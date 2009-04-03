/*
 * Debug Store support
 *
 * This provides a low-level interface to the hardware's Debug Store
 * feature that is used for branch trace store (BTS) and
 * precise-event based sampling (PEBS).
 *
 * It manages:
 * - DS and BTS hardware configuration
 * - buffer overflow handling (to be done)
 * - buffer access
 *
 * It does not do:
 * - security checking (is the caller allowed to trace the task)
 * - buffer allocation (memory accounting)
 *
 *
 * Copyright (C) 2007-2009 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, 2007-2009
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/trace_clock.h>

#include <asm/ds.h>

#include "ds_selftest.h"

/*
 * The configuration for a particular DS hardware implementation:
 */
struct ds_configuration {
	/* The name of the configuration: */
	const char		*name;

	/* The size of pointer-typed fields in DS, BTS, and PEBS: */
	unsigned char		sizeof_ptr_field;

	/* The size of a BTS/PEBS record in bytes: */
	unsigned char		sizeof_rec[2];

	/* Control bit-masks indexed by enum ds_feature: */
	unsigned long		ctl[dsf_ctl_max];
};
static DEFINE_PER_CPU(struct ds_configuration, ds_cfg_array);

#define ds_cfg per_cpu(ds_cfg_array, smp_processor_id())

/* Maximal size of a DS configuration: */
#define MAX_SIZEOF_DS		(12 * 8)

/* Maximal size of a BTS record: */
#define MAX_SIZEOF_BTS		(3 * 8)

/* BTS and PEBS buffer alignment: */
#define DS_ALIGNMENT		(1 << 3)

/* Mask of control bits in the DS MSR register: */
#define BTS_CONTROL				  \
	( ds_cfg.ctl[dsf_bts]			| \
	  ds_cfg.ctl[dsf_bts_kernel]		| \
	  ds_cfg.ctl[dsf_bts_user]		| \
	  ds_cfg.ctl[dsf_bts_overflow] )

/*
 * A BTS or PEBS tracer.
 *
 * This holds the configuration of the tracer and serves as a handle
 * to identify tracers.
 */
struct ds_tracer {
	/* The DS context (partially) owned by this tracer. */
	struct ds_context	*context;
	/* The buffer provided on ds_request() and its size in bytes. */
	void			*buffer;
	size_t			size;
};

struct bts_tracer {
	/* The common DS part: */
	struct ds_tracer	ds;

	/* The trace including the DS configuration: */
	struct bts_trace	trace;

	/* Buffer overflow notification function: */
	bts_ovfl_callback_t	ovfl;

	/* Active flags affecting trace collection. */
	unsigned int		flags;
};

struct pebs_tracer {
	/* The common DS part: */
	struct ds_tracer	ds;

	/* The trace including the DS configuration: */
	struct pebs_trace	trace;

	/* Buffer overflow notification function: */
	pebs_ovfl_callback_t	ovfl;
};

/*
 * Debug Store (DS) save area configuration (see Intel64 and IA32
 * Architectures Software Developer's Manual, section 18.5)
 *
 * The DS configuration consists of the following fields; different
 * architetures vary in the size of those fields.
 *
 * - double-word aligned base linear address of the BTS buffer
 * - write pointer into the BTS buffer
 * - end linear address of the BTS buffer (one byte beyond the end of
 *   the buffer)
 * - interrupt pointer into BTS buffer
 *   (interrupt occurs when write pointer passes interrupt pointer)
 * - double-word aligned base linear address of the PEBS buffer
 * - write pointer into the PEBS buffer
 * - end linear address of the PEBS buffer (one byte beyond the end of
 *   the buffer)
 * - interrupt pointer into PEBS buffer
 *   (interrupt occurs when write pointer passes interrupt pointer)
 * - value to which counter is reset following counter overflow
 *
 * Later architectures use 64bit pointers throughout, whereas earlier
 * architectures use 32bit pointers in 32bit mode.
 *
 *
 * We compute the base address for the first 8 fields based on:
 * - the field size stored in the DS configuration
 * - the relative field position
 * - an offset giving the start of the respective region
 *
 * This offset is further used to index various arrays holding
 * information for BTS and PEBS at the respective index.
 *
 * On later 32bit processors, we only access the lower 32bit of the
 * 64bit pointer fields. The upper halves will be zeroed out.
 */

enum ds_field {
	ds_buffer_base = 0,
	ds_index,
	ds_absolute_maximum,
	ds_interrupt_threshold,
};

enum ds_qualifier {
	ds_bts = 0,
	ds_pebs
};

static inline unsigned long
ds_get(const unsigned char *base, enum ds_qualifier qual, enum ds_field field)
{
	base += (ds_cfg.sizeof_ptr_field * (field + (4 * qual)));
	return *(unsigned long *)base;
}

static inline void
ds_set(unsigned char *base, enum ds_qualifier qual, enum ds_field field,
       unsigned long value)
{
	base += (ds_cfg.sizeof_ptr_field * (field + (4 * qual)));
	(*(unsigned long *)base) = value;
}


/*
 * Locking is done only for allocating BTS or PEBS resources.
 */
static DEFINE_SPINLOCK(ds_lock);

/*
 * We either support (system-wide) per-cpu or per-thread allocation.
 * We distinguish the two based on the task_struct pointer, where a
 * NULL pointer indicates per-cpu allocation for the current cpu.
 *
 * Allocations are use-counted. As soon as resources are allocated,
 * further allocations must be of the same type (per-cpu or
 * per-thread). We model this by counting allocations (i.e. the number
 * of tracers of a certain type) for one type negatively:
 *   =0  no tracers
 *   >0  number of per-thread tracers
 *   <0  number of per-cpu tracers
 *
 * Tracers essentially gives the number of ds contexts for a certain
 * type of allocation.
 */
static atomic_t tracers = ATOMIC_INIT(0);

static inline int get_tracer(struct task_struct *task)
{
	int error;

	spin_lock_irq(&ds_lock);

	if (task) {
		error = -EPERM;
		if (atomic_read(&tracers) < 0)
			goto out;
		atomic_inc(&tracers);
	} else {
		error = -EPERM;
		if (atomic_read(&tracers) > 0)
			goto out;
		atomic_dec(&tracers);
	}

	error = 0;
out:
	spin_unlock_irq(&ds_lock);
	return error;
}

static inline void put_tracer(struct task_struct *task)
{
	if (task)
		atomic_dec(&tracers);
	else
		atomic_inc(&tracers);
}

/*
 * The DS context is either attached to a thread or to a cpu:
 * - in the former case, the thread_struct contains a pointer to the
 *   attached context.
 * - in the latter case, we use a static array of per-cpu context
 *   pointers.
 *
 * Contexts are use-counted. They are allocated on first access and
 * deallocated when the last user puts the context.
 */
struct ds_context {
	/* The DS configuration; goes into MSR_IA32_DS_AREA: */
	unsigned char		ds[MAX_SIZEOF_DS];

	/* The owner of the BTS and PEBS configuration, respectively: */
	struct bts_tracer	*bts_master;
	struct pebs_tracer	*pebs_master;

	/* Use count: */
	unsigned long count;

	/* Pointer to the context pointer field: */
	struct ds_context	**this;

	/* The traced task; NULL for current cpu: */
	struct task_struct	*task;
};

static DEFINE_PER_CPU(struct ds_context *, system_context_array);

#define system_context per_cpu(system_context_array, smp_processor_id())


static inline struct ds_context *ds_get_context(struct task_struct *task)
{
	struct ds_context **p_context =
		(task ? &task->thread.ds_ctx : &system_context);
	struct ds_context *context = NULL;
	struct ds_context *new_context = NULL;
	unsigned long irq;

	/*
	 * Chances are small that we already have a context.
	 *
	 * Contexts for per-cpu tracing are allocated using
	 * smp_call_function(). We must not sleep.
	 */
	new_context = kzalloc(sizeof(*new_context), GFP_ATOMIC);
	if (!new_context)
		return NULL;

	spin_lock_irqsave(&ds_lock, irq);

	context = *p_context;
	if (!context) {
		context = new_context;

		context->this = p_context;
		context->task = task;
		context->count = 0;

		if (task)
			set_tsk_thread_flag(task, TIF_DS_AREA_MSR);

		if (!task || (task == current))
			wrmsrl(MSR_IA32_DS_AREA, (unsigned long)context->ds);

		*p_context = context;
	}

	context->count++;

	spin_unlock_irqrestore(&ds_lock, irq);

	if (context != new_context)
		kfree(new_context);

	return context;
}

static inline void ds_put_context(struct ds_context *context)
{
	struct task_struct *task;
	unsigned long irq;

	if (!context)
		return;

	spin_lock_irqsave(&ds_lock, irq);

	if (--context->count) {
		spin_unlock_irqrestore(&ds_lock, irq);
		return;
	}

	*(context->this) = NULL;

	task = context->task;

	if (task)
		clear_tsk_thread_flag(task, TIF_DS_AREA_MSR);

	if (!task || (task == current))
		wrmsrl(MSR_IA32_DS_AREA, 0);

	spin_unlock_irqrestore(&ds_lock, irq);

	/* The context might still be in use for context switching. */
	if (task && (task != current))
		wait_task_context_switch(task);

	kfree(context);
}


/*
 * Call the tracer's callback on a buffer overflow.
 *
 * context: the ds context
 * qual: the buffer type
 */
static void ds_overflow(struct ds_context *context, enum ds_qualifier qual)
{
	switch (qual) {
	case ds_bts:
		if (context->bts_master &&
		    context->bts_master->ovfl)
			context->bts_master->ovfl(context->bts_master);
		break;
	case ds_pebs:
		if (context->pebs_master &&
		    context->pebs_master->ovfl)
			context->pebs_master->ovfl(context->pebs_master);
		break;
	}
}


/*
 * Write raw data into the BTS or PEBS buffer.
 *
 * The remainder of any partially written record is zeroed out.
 *
 * context: the DS context
 * qual:    the buffer type
 * record:  the data to write
 * size:    the size of the data
 */
static int ds_write(struct ds_context *context, enum ds_qualifier qual,
		    const void *record, size_t size)
{
	int bytes_written = 0;

	if (!record)
		return -EINVAL;

	while (size) {
		unsigned long base, index, end, write_end, int_th;
		unsigned long write_size, adj_write_size;

		/*
		 * Write as much as possible without producing an
		 * overflow interrupt.
		 *
		 * Interrupt_threshold must either be
		 * - bigger than absolute_maximum or
		 * - point to a record between buffer_base and absolute_maximum
		 *
		 * Index points to a valid record.
		 */
		base   = ds_get(context->ds, qual, ds_buffer_base);
		index  = ds_get(context->ds, qual, ds_index);
		end    = ds_get(context->ds, qual, ds_absolute_maximum);
		int_th = ds_get(context->ds, qual, ds_interrupt_threshold);

		write_end = min(end, int_th);

		/*
		 * If we are already beyond the interrupt threshold,
		 * we fill the entire buffer.
		 */
		if (write_end <= index)
			write_end = end;

		if (write_end <= index)
			break;

		write_size = min((unsigned long) size, write_end - index);
		memcpy((void *)index, record, write_size);

		record = (const char *)record + write_size;
		size -= write_size;
		bytes_written += write_size;

		adj_write_size = write_size / ds_cfg.sizeof_rec[qual];
		adj_write_size *= ds_cfg.sizeof_rec[qual];

		/* Zero out trailing bytes. */
		memset((char *)index + write_size, 0,
		       adj_write_size - write_size);
		index += adj_write_size;

		if (index >= end)
			index = base;
		ds_set(context->ds, qual, ds_index, index);

		if (index >= int_th)
			ds_overflow(context, qual);
	}

	return bytes_written;
}


/*
 * Branch Trace Store (BTS) uses the following format. Different
 * architectures vary in the size of those fields.
 * - source linear address
 * - destination linear address
 * - flags
 *
 * Later architectures use 64bit pointers throughout, whereas earlier
 * architectures use 32bit pointers in 32bit mode.
 *
 * We compute the base address for the fields based on:
 * - the field size stored in the DS configuration
 * - the relative field position
 *
 * In order to store additional information in the BTS buffer, we use
 * a special source address to indicate that the record requires
 * special interpretation.
 *
 * Netburst indicated via a bit in the flags field whether the branch
 * was predicted; this is ignored.
 *
 * We use two levels of abstraction:
 * - the raw data level defined here
 * - an arch-independent level defined in ds.h
 */

enum bts_field {
	bts_from,
	bts_to,
	bts_flags,

	bts_qual		= bts_from,
	bts_clock		= bts_to,
	bts_pid			= bts_flags,

	bts_qual_mask		= (bts_qual_max - 1),
	bts_escape		= ((unsigned long)-1 & ~bts_qual_mask)
};

static inline unsigned long bts_get(const char *base, enum bts_field field)
{
	base += (ds_cfg.sizeof_ptr_field * field);
	return *(unsigned long *)base;
}

static inline void bts_set(char *base, enum bts_field field, unsigned long val)
{
	base += (ds_cfg.sizeof_ptr_field * field);;
	(*(unsigned long *)base) = val;
}


/*
 * The raw BTS data is architecture dependent.
 *
 * For higher-level users, we give an arch-independent view.
 * - ds.h defines struct bts_struct
 * - bts_read translates one raw bts record into a bts_struct
 * - bts_write translates one bts_struct into the raw format and
 *   writes it into the top of the parameter tracer's buffer.
 *
 * return: bytes read/written on success; -Eerrno, otherwise
 */
static int
bts_read(struct bts_tracer *tracer, const void *at, struct bts_struct *out)
{
	if (!tracer)
		return -EINVAL;

	if (at < tracer->trace.ds.begin)
		return -EINVAL;

	if (tracer->trace.ds.end < (at + tracer->trace.ds.size))
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	if ((bts_get(at, bts_qual) & ~bts_qual_mask) == bts_escape) {
		out->qualifier = (bts_get(at, bts_qual) & bts_qual_mask);
		out->variant.event.clock = bts_get(at, bts_clock);
		out->variant.event.pid = bts_get(at, bts_pid);
	} else {
		out->qualifier = bts_branch;
		out->variant.lbr.from = bts_get(at, bts_from);
		out->variant.lbr.to   = bts_get(at, bts_to);

		if (!out->variant.lbr.from && !out->variant.lbr.to)
			out->qualifier = bts_invalid;
	}

	return ds_cfg.sizeof_rec[ds_bts];
}

static int bts_write(struct bts_tracer *tracer, const struct bts_struct *in)
{
	unsigned char raw[MAX_SIZEOF_BTS];

	if (!tracer)
		return -EINVAL;

	if (MAX_SIZEOF_BTS < ds_cfg.sizeof_rec[ds_bts])
		return -EOVERFLOW;

	switch (in->qualifier) {
	case bts_invalid:
		bts_set(raw, bts_from, 0);
		bts_set(raw, bts_to, 0);
		bts_set(raw, bts_flags, 0);
		break;
	case bts_branch:
		bts_set(raw, bts_from, in->variant.lbr.from);
		bts_set(raw, bts_to,   in->variant.lbr.to);
		bts_set(raw, bts_flags, 0);
		break;
	case bts_task_arrives:
	case bts_task_departs:
		bts_set(raw, bts_qual, (bts_escape | in->qualifier));
		bts_set(raw, bts_clock, in->variant.event.clock);
		bts_set(raw, bts_pid, in->variant.event.pid);
		break;
	default:
		return -EINVAL;
	}

	return ds_write(tracer->ds.context, ds_bts, raw,
			ds_cfg.sizeof_rec[ds_bts]);
}


static void ds_write_config(struct ds_context *context,
			    struct ds_trace *cfg, enum ds_qualifier qual)
{
	unsigned char *ds = context->ds;

	ds_set(ds, qual, ds_buffer_base, (unsigned long)cfg->begin);
	ds_set(ds, qual, ds_index, (unsigned long)cfg->top);
	ds_set(ds, qual, ds_absolute_maximum, (unsigned long)cfg->end);
	ds_set(ds, qual, ds_interrupt_threshold, (unsigned long)cfg->ith);
}

static void ds_read_config(struct ds_context *context,
			   struct ds_trace *cfg, enum ds_qualifier qual)
{
	unsigned char *ds = context->ds;

	cfg->begin = (void *)ds_get(ds, qual, ds_buffer_base);
	cfg->top = (void *)ds_get(ds, qual, ds_index);
	cfg->end = (void *)ds_get(ds, qual, ds_absolute_maximum);
	cfg->ith = (void *)ds_get(ds, qual, ds_interrupt_threshold);
}

static void ds_init_ds_trace(struct ds_trace *trace, enum ds_qualifier qual,
			     void *base, size_t size, size_t ith,
			     unsigned int flags) {
	unsigned long buffer, adj;

	/*
	 * Adjust the buffer address and size to meet alignment
	 * constraints:
	 * - buffer is double-word aligned
	 * - size is multiple of record size
	 *
	 * We checked the size at the very beginning; we have enough
	 * space to do the adjustment.
	 */
	buffer = (unsigned long)base;

	adj = ALIGN(buffer, DS_ALIGNMENT) - buffer;
	buffer += adj;
	size   -= adj;

	trace->n = size / ds_cfg.sizeof_rec[qual];
	trace->size = ds_cfg.sizeof_rec[qual];

	size = (trace->n * trace->size);

	trace->begin = (void *)buffer;
	trace->top = trace->begin;
	trace->end = (void *)(buffer + size);
	/*
	 * The value for 'no threshold' is -1, which will set the
	 * threshold outside of the buffer, just like we want it.
	 */
	trace->ith = (void *)(buffer + size - ith);

	trace->flags = flags;
}


static int ds_request(struct ds_tracer *tracer, struct ds_trace *trace,
		      enum ds_qualifier qual, struct task_struct *task,
		      void *base, size_t size, size_t th, unsigned int flags)
{
	struct ds_context *context;
	int error;

	error = -EOPNOTSUPP;
	if (!ds_cfg.sizeof_rec[qual])
		goto out;

	error = -EINVAL;
	if (!base)
		goto out;

	/* We require some space to do alignment adjustments below. */
	error = -EINVAL;
	if (size < (DS_ALIGNMENT + ds_cfg.sizeof_rec[qual]))
		goto out;

	if (th != (size_t)-1) {
		th *= ds_cfg.sizeof_rec[qual];

		error = -EINVAL;
		if (size <= th)
			goto out;
	}

	tracer->buffer = base;
	tracer->size = size;

	error = -ENOMEM;
	context = ds_get_context(task);
	if (!context)
		goto out;
	tracer->context = context;

	ds_init_ds_trace(trace, qual, base, size, th, flags);

	error = 0;
 out:
	return error;
}

struct bts_tracer *ds_request_bts(struct task_struct *task,
				  void *base, size_t size,
				  bts_ovfl_callback_t ovfl, size_t th,
				  unsigned int flags)
{
	struct bts_tracer *tracer;
	unsigned long irq;
	int error;

	/* Buffer overflow notification is not yet implemented. */
	error = -EOPNOTSUPP;
	if (ovfl)
		goto out;

	error = get_tracer(task);
	if (error < 0)
		goto out;

	/*
	 * Per-cpu tracing is typically requested using smp_call_function().
	 * We must not sleep.
	 */
	error = -ENOMEM;
	tracer = kzalloc(sizeof(*tracer), GFP_ATOMIC);
	if (!tracer)
		goto out_put_tracer;
	tracer->ovfl = ovfl;

	error = ds_request(&tracer->ds, &tracer->trace.ds,
			   ds_bts, task, base, size, th, flags);
	if (error < 0)
		goto out_tracer;


	spin_lock_irqsave(&ds_lock, irq);

	error = -EPERM;
	if (tracer->ds.context->bts_master)
		goto out_unlock;
	tracer->ds.context->bts_master = tracer;

	spin_unlock_irqrestore(&ds_lock, irq);


	tracer->trace.read  = bts_read;
	tracer->trace.write = bts_write;

	ds_write_config(tracer->ds.context, &tracer->trace.ds, ds_bts);
	ds_resume_bts(tracer);

	return tracer;

 out_unlock:
	spin_unlock_irqrestore(&ds_lock, irq);
	ds_put_context(tracer->ds.context);
 out_tracer:
	kfree(tracer);
 out_put_tracer:
	put_tracer(task);
 out:
	return ERR_PTR(error);
}

struct pebs_tracer *ds_request_pebs(struct task_struct *task,
				    void *base, size_t size,
				    pebs_ovfl_callback_t ovfl, size_t th,
				    unsigned int flags)
{
	struct pebs_tracer *tracer;
	unsigned long irq;
	int error;

	/* Buffer overflow notification is not yet implemented. */
	error = -EOPNOTSUPP;
	if (ovfl)
		goto out;

	error = get_tracer(task);
	if (error < 0)
		goto out;

	/*
	 * Per-cpu tracing is typically requested using smp_call_function().
	 * We must not sleep.
	 */
	error = -ENOMEM;
	tracer = kzalloc(sizeof(*tracer), GFP_ATOMIC);
	if (!tracer)
		goto out_put_tracer;
	tracer->ovfl = ovfl;

	error = ds_request(&tracer->ds, &tracer->trace.ds,
			   ds_pebs, task, base, size, th, flags);
	if (error < 0)
		goto out_tracer;

	spin_lock_irqsave(&ds_lock, irq);

	error = -EPERM;
	if (tracer->ds.context->pebs_master)
		goto out_unlock;
	tracer->ds.context->pebs_master = tracer;

	spin_unlock_irqrestore(&ds_lock, irq);

	ds_write_config(tracer->ds.context, &tracer->trace.ds, ds_pebs);
	ds_resume_pebs(tracer);

	return tracer;

 out_unlock:
	spin_unlock_irqrestore(&ds_lock, irq);
	ds_put_context(tracer->ds.context);
 out_tracer:
	kfree(tracer);
 out_put_tracer:
	put_tracer(task);
 out:
	return ERR_PTR(error);
}

void ds_release_bts(struct bts_tracer *tracer)
{
	struct task_struct *task;

	if (!tracer)
		return;

	task = tracer->ds.context->task;

	ds_suspend_bts(tracer);

	WARN_ON_ONCE(tracer->ds.context->bts_master != tracer);
	tracer->ds.context->bts_master = NULL;

	/* Make sure tracing stopped and the tracer is not in use. */
	if (task && (task != current))
		wait_task_context_switch(task);

	ds_put_context(tracer->ds.context);
	put_tracer(task);

	kfree(tracer);
}

void ds_suspend_bts(struct bts_tracer *tracer)
{
	struct task_struct *task;

	if (!tracer)
		return;

	tracer->flags = 0;

	task = tracer->ds.context->task;

	if (!task || (task == current))
		update_debugctlmsr(get_debugctlmsr() & ~BTS_CONTROL);

	if (task) {
		task->thread.debugctlmsr &= ~BTS_CONTROL;

		if (!task->thread.debugctlmsr)
			clear_tsk_thread_flag(task, TIF_DEBUGCTLMSR);
	}
}

void ds_resume_bts(struct bts_tracer *tracer)
{
	struct task_struct *task;
	unsigned long control;

	if (!tracer)
		return;

	tracer->flags = tracer->trace.ds.flags;

	task = tracer->ds.context->task;

	control = ds_cfg.ctl[dsf_bts];
	if (!(tracer->trace.ds.flags & BTS_KERNEL))
		control |= ds_cfg.ctl[dsf_bts_kernel];
	if (!(tracer->trace.ds.flags & BTS_USER))
		control |= ds_cfg.ctl[dsf_bts_user];

	if (task) {
		task->thread.debugctlmsr |= control;
		set_tsk_thread_flag(task, TIF_DEBUGCTLMSR);
	}

	if (!task || (task == current))
		update_debugctlmsr(get_debugctlmsr() | control);
}

void ds_release_pebs(struct pebs_tracer *tracer)
{
	struct task_struct *task;

	if (!tracer)
		return;

	task = tracer->ds.context->task;

	ds_suspend_pebs(tracer);

	WARN_ON_ONCE(tracer->ds.context->pebs_master != tracer);
	tracer->ds.context->pebs_master = NULL;

	ds_put_context(tracer->ds.context);
	put_tracer(task);

	kfree(tracer);
}

void ds_suspend_pebs(struct pebs_tracer *tracer)
{

}

void ds_resume_pebs(struct pebs_tracer *tracer)
{

}

const struct bts_trace *ds_read_bts(struct bts_tracer *tracer)
{
	if (!tracer)
		return NULL;

	ds_read_config(tracer->ds.context, &tracer->trace.ds, ds_bts);
	return &tracer->trace;
}

const struct pebs_trace *ds_read_pebs(struct pebs_tracer *tracer)
{
	if (!tracer)
		return NULL;

	ds_read_config(tracer->ds.context, &tracer->trace.ds, ds_pebs);
	tracer->trace.reset_value =
		*(u64 *)(tracer->ds.context->ds +
			 (ds_cfg.sizeof_ptr_field * 8));

	return &tracer->trace;
}

int ds_reset_bts(struct bts_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	tracer->trace.ds.top = tracer->trace.ds.begin;

	ds_set(tracer->ds.context->ds, ds_bts, ds_index,
	       (unsigned long)tracer->trace.ds.top);

	return 0;
}

int ds_reset_pebs(struct pebs_tracer *tracer)
{
	if (!tracer)
		return -EINVAL;

	tracer->trace.ds.top = tracer->trace.ds.begin;

	ds_set(tracer->ds.context->ds, ds_bts, ds_index,
	       (unsigned long)tracer->trace.ds.top);

	return 0;
}

int ds_set_pebs_reset(struct pebs_tracer *tracer, u64 value)
{
	if (!tracer)
		return -EINVAL;

	*(u64 *)(tracer->ds.context->ds +
		 (ds_cfg.sizeof_ptr_field * 8)) = value;

	return 0;
}

static const struct ds_configuration ds_cfg_netburst = {
	.name = "Netburst",
	.ctl[dsf_bts]		= (1 << 2) | (1 << 3),
	.ctl[dsf_bts_kernel]	= (1 << 5),
	.ctl[dsf_bts_user]	= (1 << 6),
};
static const struct ds_configuration ds_cfg_pentium_m = {
	.name = "Pentium M",
	.ctl[dsf_bts]		= (1 << 6) | (1 << 7),
};
static const struct ds_configuration ds_cfg_core2_atom = {
	.name = "Core 2/Atom",
	.ctl[dsf_bts]		= (1 << 6) | (1 << 7),
	.ctl[dsf_bts_kernel]	= (1 << 9),
	.ctl[dsf_bts_user]	= (1 << 10),
};

static void
ds_configure(const struct ds_configuration *cfg,
	     struct cpuinfo_x86 *cpu)
{
	unsigned long nr_pebs_fields = 0;

	printk(KERN_INFO "[ds] using %s configuration\n", cfg->name);

#ifdef __i386__
	nr_pebs_fields = 10;
#else
	nr_pebs_fields = 18;
#endif

	memset(&ds_cfg, 0, sizeof(ds_cfg));
	ds_cfg = *cfg;

	ds_cfg.sizeof_ptr_field =
		(cpu_has(cpu, X86_FEATURE_DTES64) ? 8 : 4);

	ds_cfg.sizeof_rec[ds_bts]  = ds_cfg.sizeof_ptr_field * 3;
	ds_cfg.sizeof_rec[ds_pebs] = ds_cfg.sizeof_ptr_field * nr_pebs_fields;

	if (!cpu_has(cpu, X86_FEATURE_BTS)) {
		ds_cfg.sizeof_rec[ds_bts] = 0;
		printk(KERN_INFO "[ds] bts not available\n");
	}
	if (!cpu_has(cpu, X86_FEATURE_PEBS)) {
		ds_cfg.sizeof_rec[ds_pebs] = 0;
		printk(KERN_INFO "[ds] pebs not available\n");
	}

	if (ds_cfg.sizeof_rec[ds_bts]) {
		int error;

		error = ds_selftest_bts();
		if (error) {
			WARN(1, "[ds] selftest failed. disabling bts.\n");
			ds_cfg.sizeof_rec[ds_bts] = 0;
		}
	}

	if (ds_cfg.sizeof_rec[ds_pebs]) {
		int error;

		error = ds_selftest_pebs();
		if (error) {
			WARN(1, "[ds] selftest failed. disabling pebs.\n");
			ds_cfg.sizeof_rec[ds_pebs] = 0;
		}
	}

	printk(KERN_INFO "[ds] sizes: address: %u bit, ",
	       8 * ds_cfg.sizeof_ptr_field);
	printk("bts/pebs record: %u/%u bytes\n",
	       ds_cfg.sizeof_rec[ds_bts], ds_cfg.sizeof_rec[ds_pebs]);

	WARN_ON_ONCE(MAX_SIZEOF_DS < (12 * ds_cfg.sizeof_ptr_field));
}

void __cpuinit ds_init_intel(struct cpuinfo_x86 *c)
{
	switch (c->x86) {
	case 0x6:
		switch (c->x86_model) {
		case 0x9:
		case 0xd: /* Pentium M */
			ds_configure(&ds_cfg_pentium_m, c);
			break;
		case 0xf:
		case 0x17: /* Core2 */
		case 0x1c: /* Atom */
			ds_configure(&ds_cfg_core2_atom, c);
			break;
		case 0x1a: /* Core i7 */
		default:
			/* Sorry, don't know about them. */
			break;
		}
		break;
	case 0xf:
		switch (c->x86_model) {
		case 0x0:
		case 0x1:
		case 0x2: /* Netburst */
			ds_configure(&ds_cfg_netburst, c);
			break;
		default:
			/* Sorry, don't know about them. */
			break;
		}
		break;
	default:
		/* Sorry, don't know about them. */
		break;
	}
}

static inline void ds_take_timestamp(struct ds_context *context,
				     enum bts_qualifier qualifier,
				     struct task_struct *task)
{
	struct bts_tracer *tracer = context->bts_master;
	struct bts_struct ts;

	/* Prevent compilers from reading the tracer pointer twice. */
	barrier();

	if (!tracer || !(tracer->flags & BTS_TIMESTAMPS))
		return;

	memset(&ts, 0, sizeof(ts));
	ts.qualifier		= qualifier;
	ts.variant.event.clock	= trace_clock_global();
	ts.variant.event.pid	= task->pid;

	bts_write(tracer, &ts);
}

/*
 * Change the DS configuration from tracing prev to tracing next.
 */
void ds_switch_to(struct task_struct *prev, struct task_struct *next)
{
	struct ds_context *prev_ctx	= prev->thread.ds_ctx;
	struct ds_context *next_ctx	= next->thread.ds_ctx;
	unsigned long debugctlmsr	= next->thread.debugctlmsr;

	/* Make sure all data is read before we start. */
	barrier();

	if (prev_ctx) {
		update_debugctlmsr(0);

		ds_take_timestamp(prev_ctx, bts_task_departs, prev);
	}

	if (next_ctx) {
		ds_take_timestamp(next_ctx, bts_task_arrives, next);

		wrmsrl(MSR_IA32_DS_AREA, (unsigned long)next_ctx->ds);
	}

	update_debugctlmsr(debugctlmsr);
}

void ds_copy_thread(struct task_struct *tsk, struct task_struct *father)
{
	clear_tsk_thread_flag(tsk, TIF_DS_AREA_MSR);
	tsk->thread.ds_ctx = NULL;
}

void ds_exit_thread(struct task_struct *tsk)
{
}
