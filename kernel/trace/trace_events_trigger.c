/*
 * trace_events_trigger - trace event triggers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2013 Tom Zanussi <tom.zanussi@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "trace.h"

static LIST_HEAD(trigger_commands);
static DEFINE_MUTEX(trigger_cmd_mutex);

static void
trigger_data_free(struct event_trigger_data *data)
{
	synchronize_sched(); /* make sure current triggers exit before free */
	kfree(data);
}

/**
 * event_triggers_call - Call triggers associated with a trace event
 * @file: The ftrace_event_file associated with the event
 *
 * For each trigger associated with an event, invoke the trigger
 * function registered with the associated trigger command.
 *
 * Called from tracepoint handlers (with rcu_read_lock_sched() held).
 *
 * Return: an enum event_trigger_type value containing a set bit for
 * any trigger that should be deferred, ETT_NONE if nothing to defer.
 */
void event_triggers_call(struct ftrace_event_file *file)
{
	struct event_trigger_data *data;

	if (list_empty(&file->triggers))
		return;

	list_for_each_entry_rcu(data, &file->triggers, list)
		data->ops->func(data);
}
EXPORT_SYMBOL_GPL(event_triggers_call);

static void *trigger_next(struct seq_file *m, void *t, loff_t *pos)
{
	struct ftrace_event_file *event_file = event_file_data(m->private);

	return seq_list_next(t, &event_file->triggers, pos);
}

static void *trigger_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_event_file *event_file;

	/* ->stop() is called even if ->start() fails */
	mutex_lock(&event_mutex);
	event_file = event_file_data(m->private);
	if (unlikely(!event_file))
		return ERR_PTR(-ENODEV);

	return seq_list_start(&event_file->triggers, *pos);
}

static void trigger_stop(struct seq_file *m, void *t)
{
	mutex_unlock(&event_mutex);
}

static int trigger_show(struct seq_file *m, void *v)
{
	struct event_trigger_data *data;

	data = list_entry(v, struct event_trigger_data, list);
	data->ops->print(m, data->ops, data);

	return 0;
}

static const struct seq_operations event_triggers_seq_ops = {
	.start = trigger_start,
	.next = trigger_next,
	.stop = trigger_stop,
	.show = trigger_show,
};

static int event_trigger_regex_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	mutex_lock(&event_mutex);

	if (unlikely(!event_file_data(file))) {
		mutex_unlock(&event_mutex);
		return -ENODEV;
	}

	if (file->f_mode & FMODE_READ) {
		ret = seq_open(file, &event_triggers_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = file;
		}
	}

	mutex_unlock(&event_mutex);

	return ret;
}

static int trigger_process_regex(struct ftrace_event_file *file, char *buff)
{
	char *command, *next = buff;
	struct event_command *p;
	int ret = -EINVAL;

	command = strsep(&next, ": \t");
	command = (command[0] != '!') ? command : command + 1;

	mutex_lock(&trigger_cmd_mutex);
	list_for_each_entry(p, &trigger_commands, list) {
		if (strcmp(p->name, command) == 0) {
			ret = p->func(p, file, buff, command, next);
			goto out_unlock;
		}
	}
 out_unlock:
	mutex_unlock(&trigger_cmd_mutex);

	return ret;
}

static ssize_t event_trigger_regex_write(struct file *file,
					 const char __user *ubuf,
					 size_t cnt, loff_t *ppos)
{
	struct ftrace_event_file *event_file;
	ssize_t ret;
	char *buf;

	if (!cnt)
		return 0;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = (char *)__get_free_page(GFP_TEMPORARY);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		free_page((unsigned long)buf);
		return -EFAULT;
	}
	buf[cnt] = '\0';
	strim(buf);

	mutex_lock(&event_mutex);
	event_file = event_file_data(file);
	if (unlikely(!event_file)) {
		mutex_unlock(&event_mutex);
		free_page((unsigned long)buf);
		return -ENODEV;
	}
	ret = trigger_process_regex(event_file, buf);
	mutex_unlock(&event_mutex);

	free_page((unsigned long)buf);
	if (ret < 0)
		goto out;

	*ppos += cnt;
	ret = cnt;
 out:
	return ret;
}

static int event_trigger_regex_release(struct inode *inode, struct file *file)
{
	mutex_lock(&event_mutex);

	if (file->f_mode & FMODE_READ)
		seq_release(inode, file);

	mutex_unlock(&event_mutex);

	return 0;
}

static ssize_t
event_trigger_write(struct file *filp, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	return event_trigger_regex_write(filp, ubuf, cnt, ppos);
}

static int
event_trigger_open(struct inode *inode, struct file *filp)
{
	return event_trigger_regex_open(inode, filp);
}

static int
event_trigger_release(struct inode *inode, struct file *file)
{
	return event_trigger_regex_release(inode, file);
}

const struct file_operations event_trigger_fops = {
	.open = event_trigger_open,
	.read = seq_read,
	.write = event_trigger_write,
	.llseek = ftrace_filter_lseek,
	.release = event_trigger_release,
};

/*
 * Currently we only register event commands from __init, so mark this
 * __init too.
 */
static __init int register_event_command(struct event_command *cmd)
{
	struct event_command *p;
	int ret = 0;

	mutex_lock(&trigger_cmd_mutex);
	list_for_each_entry(p, &trigger_commands, list) {
		if (strcmp(cmd->name, p->name) == 0) {
			ret = -EBUSY;
			goto out_unlock;
		}
	}
	list_add(&cmd->list, &trigger_commands);
 out_unlock:
	mutex_unlock(&trigger_cmd_mutex);

	return ret;
}

/*
 * Currently we only unregister event commands from __init, so mark
 * this __init too.
 */
static __init int unregister_event_command(struct event_command *cmd)
{
	struct event_command *p, *n;
	int ret = -ENODEV;

	mutex_lock(&trigger_cmd_mutex);
	list_for_each_entry_safe(p, n, &trigger_commands, list) {
		if (strcmp(cmd->name, p->name) == 0) {
			ret = 0;
			list_del_init(&p->list);
			goto out_unlock;
		}
	}
 out_unlock:
	mutex_unlock(&trigger_cmd_mutex);

	return ret;
}

/**
 * event_trigger_print - Generic event_trigger_ops @print implementation
 * @name: The name of the event trigger
 * @m: The seq_file being printed to
 * @data: Trigger-specific data
 * @filter_str: filter_str to print, if present
 *
 * Common implementation for event triggers to print themselves.
 *
 * Usually wrapped by a function that simply sets the @name of the
 * trigger command and then invokes this.
 *
 * Return: 0 on success, errno otherwise
 */
static int
event_trigger_print(const char *name, struct seq_file *m,
		    void *data, char *filter_str)
{
	long count = (long)data;

	seq_printf(m, "%s", name);

	if (count == -1)
		seq_puts(m, ":unlimited");
	else
		seq_printf(m, ":count=%ld", count);

	if (filter_str)
		seq_printf(m, " if %s\n", filter_str);
	else
		seq_puts(m, "\n");

	return 0;
}

/**
 * event_trigger_init - Generic event_trigger_ops @init implementation
 * @ops: The trigger ops associated with the trigger
 * @data: Trigger-specific data
 *
 * Common implementation of event trigger initialization.
 *
 * Usually used directly as the @init method in event trigger
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
static int
event_trigger_init(struct event_trigger_ops *ops,
		   struct event_trigger_data *data)
{
	data->ref++;
	return 0;
}

/**
 * event_trigger_free - Generic event_trigger_ops @free implementation
 * @ops: The trigger ops associated with the trigger
 * @data: Trigger-specific data
 *
 * Common implementation of event trigger de-initialization.
 *
 * Usually used directly as the @free method in event trigger
 * implementations.
 */
static void
event_trigger_free(struct event_trigger_ops *ops,
		   struct event_trigger_data *data)
{
	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref)
		trigger_data_free(data);
}

static int trace_event_trigger_enable_disable(struct ftrace_event_file *file,
					      int trigger_enable)
{
	int ret = 0;

	if (trigger_enable) {
		if (atomic_inc_return(&file->tm_ref) > 1)
			return ret;
		set_bit(FTRACE_EVENT_FL_TRIGGER_MODE_BIT, &file->flags);
		ret = trace_event_enable_disable(file, 1, 1);
	} else {
		if (atomic_dec_return(&file->tm_ref) > 0)
			return ret;
		clear_bit(FTRACE_EVENT_FL_TRIGGER_MODE_BIT, &file->flags);
		ret = trace_event_enable_disable(file, 0, 1);
	}

	return ret;
}

/**
 * clear_event_triggers - Clear all triggers associated with a trace array
 * @tr: The trace array to clear
 *
 * For each trigger, the triggering event has its tm_ref decremented
 * via trace_event_trigger_enable_disable(), and any associated event
 * (in the case of enable/disable_event triggers) will have its sm_ref
 * decremented via free()->trace_event_enable_disable().  That
 * combination effectively reverses the soft-mode/trigger state added
 * by trigger registration.
 *
 * Must be called with event_mutex held.
 */
void
clear_event_triggers(struct trace_array *tr)
{
	struct ftrace_event_file *file;

	list_for_each_entry(file, &tr->events, list) {
		struct event_trigger_data *data;
		list_for_each_entry_rcu(data, &file->triggers, list) {
			trace_event_trigger_enable_disable(file, 0);
			if (data->ops->free)
				data->ops->free(data->ops, data);
		}
	}
}

/**
 * register_trigger - Generic event_command @reg implementation
 * @glob: The raw string used to register the trigger
 * @ops: The trigger ops associated with the trigger
 * @data: Trigger-specific data to associate with the trigger
 * @file: The ftrace_event_file associated with the event
 *
 * Common implementation for event trigger registration.
 *
 * Usually used directly as the @reg method in event command
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
static int register_trigger(char *glob, struct event_trigger_ops *ops,
			    struct event_trigger_data *data,
			    struct ftrace_event_file *file)
{
	struct event_trigger_data *test;
	int ret = 0;

	list_for_each_entry_rcu(test, &file->triggers, list) {
		if (test->cmd_ops->trigger_type == data->cmd_ops->trigger_type) {
			ret = -EEXIST;
			goto out;
		}
	}

	if (data->ops->init) {
		ret = data->ops->init(data->ops, data);
		if (ret < 0)
			goto out;
	}

	list_add_rcu(&data->list, &file->triggers);
	ret++;

	if (trace_event_trigger_enable_disable(file, 1) < 0) {
		list_del_rcu(&data->list);
		ret--;
	}
out:
	return ret;
}

/**
 * unregister_trigger - Generic event_command @unreg implementation
 * @glob: The raw string used to register the trigger
 * @ops: The trigger ops associated with the trigger
 * @test: Trigger-specific data used to find the trigger to remove
 * @file: The ftrace_event_file associated with the event
 *
 * Common implementation for event trigger unregistration.
 *
 * Usually used directly as the @unreg method in event command
 * implementations.
 */
static void unregister_trigger(char *glob, struct event_trigger_ops *ops,
			       struct event_trigger_data *test,
			       struct ftrace_event_file *file)
{
	struct event_trigger_data *data;
	bool unregistered = false;

	list_for_each_entry_rcu(data, &file->triggers, list) {
		if (data->cmd_ops->trigger_type == test->cmd_ops->trigger_type) {
			unregistered = true;
			list_del_rcu(&data->list);
			trace_event_trigger_enable_disable(file, 0);
			break;
		}
	}

	if (unregistered && data->ops->free)
		data->ops->free(data->ops, data);
}

/**
 * event_trigger_callback - Generic event_command @func implementation
 * @cmd_ops: The command ops, used for trigger registration
 * @file: The ftrace_event_file associated with the event
 * @glob: The raw string used to register the trigger
 * @cmd: The cmd portion of the string used to register the trigger
 * @param: The params portion of the string used to register the trigger
 *
 * Common implementation for event command parsing and trigger
 * instantiation.
 *
 * Usually used directly as the @func method in event command
 * implementations.
 *
 * Return: 0 on success, errno otherwise
 */
static int
event_trigger_callback(struct event_command *cmd_ops,
		       struct ftrace_event_file *file,
		       char *glob, char *cmd, char *param)
{
	struct event_trigger_data *trigger_data;
	struct event_trigger_ops *trigger_ops;
	char *trigger = NULL;
	char *number;
	int ret;

	/* separate the trigger from the filter (t:n [if filter]) */
	if (param && isdigit(param[0]))
		trigger = strsep(&param, " \t");

	trigger_ops = cmd_ops->get_trigger_ops(cmd, trigger);

	ret = -ENOMEM;
	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		goto out;

	trigger_data->count = -1;
	trigger_data->ops = trigger_ops;
	trigger_data->cmd_ops = cmd_ops;
	INIT_LIST_HEAD(&trigger_data->list);

	if (glob[0] == '!') {
		cmd_ops->unreg(glob+1, trigger_ops, trigger_data, file);
		kfree(trigger_data);
		ret = 0;
		goto out;
	}

	if (trigger) {
		number = strsep(&trigger, ":");

		ret = -EINVAL;
		if (!strlen(number))
			goto out_free;

		/*
		 * We use the callback data field (which is a pointer)
		 * as our counter.
		 */
		ret = kstrtoul(number, 0, &trigger_data->count);
		if (ret)
			goto out_free;
	}

	if (!param) /* if param is non-empty, it's supposed to be a filter */
		goto out_reg;

	if (!cmd_ops->set_filter)
		goto out_reg;

	ret = cmd_ops->set_filter(param, trigger_data, file);
	if (ret < 0)
		goto out_free;

 out_reg:
	ret = cmd_ops->reg(glob, trigger_ops, trigger_data, file);
	/*
	 * The above returns on success the # of functions enabled,
	 * but if it didn't find any functions it returns zero.
	 * Consider no functions a failure too.
	 */
	if (!ret) {
		ret = -ENOENT;
		goto out_free;
	} else if (ret < 0)
		goto out_free;
	ret = 0;
 out:
	return ret;

 out_free:
	kfree(trigger_data);
	goto out;
}

static void
traceon_trigger(struct event_trigger_data *data)
{
	if (tracing_is_on())
		return;

	tracing_on();
}

static void
traceon_count_trigger(struct event_trigger_data *data)
{
	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	traceon_trigger(data);
}

static void
traceoff_trigger(struct event_trigger_data *data)
{
	if (!tracing_is_on())
		return;

	tracing_off();
}

static void
traceoff_count_trigger(struct event_trigger_data *data)
{
	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	traceoff_trigger(data);
}

static int
traceon_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
		      struct event_trigger_data *data)
{
	return event_trigger_print("traceon", m, (void *)data->count,
				   data->filter_str);
}

static int
traceoff_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
		       struct event_trigger_data *data)
{
	return event_trigger_print("traceoff", m, (void *)data->count,
				   data->filter_str);
}

static struct event_trigger_ops traceon_trigger_ops = {
	.func			= traceon_trigger,
	.print			= traceon_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops traceon_count_trigger_ops = {
	.func			= traceon_count_trigger,
	.print			= traceon_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops traceoff_trigger_ops = {
	.func			= traceoff_trigger,
	.print			= traceoff_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops traceoff_count_trigger_ops = {
	.func			= traceoff_count_trigger,
	.print			= traceoff_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops *
onoff_get_trigger_ops(char *cmd, char *param)
{
	struct event_trigger_ops *ops;

	/* we register both traceon and traceoff to this callback */
	if (strcmp(cmd, "traceon") == 0)
		ops = param ? &traceon_count_trigger_ops :
			&traceon_trigger_ops;
	else
		ops = param ? &traceoff_count_trigger_ops :
			&traceoff_trigger_ops;

	return ops;
}

static struct event_command trigger_traceon_cmd = {
	.name			= "traceon",
	.trigger_type		= ETT_TRACE_ONOFF,
	.func			= event_trigger_callback,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= onoff_get_trigger_ops,
};

static struct event_command trigger_traceoff_cmd = {
	.name			= "traceoff",
	.trigger_type		= ETT_TRACE_ONOFF,
	.func			= event_trigger_callback,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= onoff_get_trigger_ops,
};

#ifdef CONFIG_TRACER_SNAPSHOT
static void
snapshot_trigger(struct event_trigger_data *data)
{
	tracing_snapshot();
}

static void
snapshot_count_trigger(struct event_trigger_data *data)
{
	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	snapshot_trigger(data);
}

static int
register_snapshot_trigger(char *glob, struct event_trigger_ops *ops,
			  struct event_trigger_data *data,
			  struct ftrace_event_file *file)
{
	int ret = register_trigger(glob, ops, data, file);

	if (ret > 0 && tracing_alloc_snapshot() != 0) {
		unregister_trigger(glob, ops, data, file);
		ret = 0;
	}

	return ret;
}

static int
snapshot_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
		       struct event_trigger_data *data)
{
	return event_trigger_print("snapshot", m, (void *)data->count,
				   data->filter_str);
}

static struct event_trigger_ops snapshot_trigger_ops = {
	.func			= snapshot_trigger,
	.print			= snapshot_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops snapshot_count_trigger_ops = {
	.func			= snapshot_count_trigger,
	.print			= snapshot_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops *
snapshot_get_trigger_ops(char *cmd, char *param)
{
	return param ? &snapshot_count_trigger_ops : &snapshot_trigger_ops;
}

static struct event_command trigger_snapshot_cmd = {
	.name			= "snapshot",
	.trigger_type		= ETT_SNAPSHOT,
	.func			= event_trigger_callback,
	.reg			= register_snapshot_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= snapshot_get_trigger_ops,
};

static __init int register_trigger_snapshot_cmd(void)
{
	int ret;

	ret = register_event_command(&trigger_snapshot_cmd);
	WARN_ON(ret < 0);

	return ret;
}
#else
static __init int register_trigger_snapshot_cmd(void) { return 0; }
#endif /* CONFIG_TRACER_SNAPSHOT */

#ifdef CONFIG_STACKTRACE
/*
 * Skip 3:
 *   stacktrace_trigger()
 *   event_triggers_post_call()
 *   ftrace_raw_event_xxx()
 */
#define STACK_SKIP 3

static void
stacktrace_trigger(struct event_trigger_data *data)
{
	trace_dump_stack(STACK_SKIP);
}

static void
stacktrace_count_trigger(struct event_trigger_data *data)
{
	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	stacktrace_trigger(data);
}

static int
stacktrace_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
			 struct event_trigger_data *data)
{
	return event_trigger_print("stacktrace", m, (void *)data->count,
				   data->filter_str);
}

static struct event_trigger_ops stacktrace_trigger_ops = {
	.func			= stacktrace_trigger,
	.print			= stacktrace_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops stacktrace_count_trigger_ops = {
	.func			= stacktrace_count_trigger,
	.print			= stacktrace_trigger_print,
	.init			= event_trigger_init,
	.free			= event_trigger_free,
};

static struct event_trigger_ops *
stacktrace_get_trigger_ops(char *cmd, char *param)
{
	return param ? &stacktrace_count_trigger_ops : &stacktrace_trigger_ops;
}

static struct event_command trigger_stacktrace_cmd = {
	.name			= "stacktrace",
	.trigger_type		= ETT_STACKTRACE,
	.post_trigger		= true,
	.func			= event_trigger_callback,
	.reg			= register_trigger,
	.unreg			= unregister_trigger,
	.get_trigger_ops	= stacktrace_get_trigger_ops,
};

static __init int register_trigger_stacktrace_cmd(void)
{
	int ret;

	ret = register_event_command(&trigger_stacktrace_cmd);
	WARN_ON(ret < 0);

	return ret;
}
#else
static __init int register_trigger_stacktrace_cmd(void) { return 0; }
#endif /* CONFIG_STACKTRACE */

static __init void unregister_trigger_traceon_traceoff_cmds(void)
{
	unregister_event_command(&trigger_traceon_cmd);
	unregister_event_command(&trigger_traceoff_cmd);
}

static __init int register_trigger_traceon_traceoff_cmds(void)
{
	int ret;

	ret = register_event_command(&trigger_traceon_cmd);
	if (WARN_ON(ret < 0))
		return ret;
	ret = register_event_command(&trigger_traceoff_cmd);
	if (WARN_ON(ret < 0))
		unregister_trigger_traceon_traceoff_cmds();

	return ret;
}

__init int register_trigger_cmds(void)
{
	register_trigger_traceon_traceoff_cmds();
	register_trigger_snapshot_cmd();
	register_trigger_stacktrace_cmd();

	return 0;
}
