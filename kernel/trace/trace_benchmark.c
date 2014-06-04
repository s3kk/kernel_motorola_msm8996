#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/trace_clock.h>

#define CREATE_TRACE_POINTS
#include "trace_benchmark.h"

static struct task_struct *bm_event_thread;

static char bm_str[BENCHMARK_EVENT_STRLEN] = "START";

static u64 bm_total;
static u64 bm_totalsq;
static u64 bm_last;
static u64 bm_max;
static u64 bm_min;
static u64 bm_first;
static s64 bm_cnt;

/*
 * This gets called in a loop recording the time it took to write
 * the tracepoint. What it writes is the time statistics of the last
 * tracepoint write. As there is nothing to write the first time
 * it simply writes "START". As the first write is cold cache and
 * the rest is hot, we save off that time in bm_first and it is
 * reported as "first", which is shown in the second write to the
 * tracepoint. The "first" field is writen within the statics from
 * then on but never changes.
 */
static void trace_do_benchmark(void)
{
	u64 start;
	u64 stop;
	u64 delta;
	s64 stddev;
	u64 seed;
	u64 last_seed;
	unsigned int avg;
	unsigned int std = 0;

	/* Only run if the tracepoint is actually active */
	if (!trace_benchmark_event_enabled())
		return;

	local_irq_disable();
	start = trace_clock_local();
	trace_benchmark_event(bm_str);
	stop = trace_clock_local();
	local_irq_enable();

	bm_cnt++;

	delta = stop - start;

	/*
	 * The first read is cold cached, keep it separate from the
	 * other calculations.
	 */
	if (bm_cnt == 1) {
		bm_first = delta;
		scnprintf(bm_str, BENCHMARK_EVENT_STRLEN,
			  "first=%llu [COLD CACHED]", bm_first);
		return;
	}

	bm_last = delta;

	bm_total += delta;
	bm_totalsq += delta * delta;

	if (delta > bm_max)
		bm_max = delta;
	if (!bm_min || delta < bm_min)
		bm_min = delta;

	if (bm_cnt > 1) {
		/*
		 * Apply Welford's method to calculate standard deviation:
		 * s^2 = 1 / (n * (n-1)) * (n * \Sum (x_i)^2 - (\Sum x_i)^2)
		 */
		stddev = (u64)bm_cnt * bm_totalsq - bm_total * bm_total;
		do_div(stddev, bm_cnt);
		do_div(stddev, bm_cnt - 1);
	} else
		stddev = 0;

	delta = bm_total;
	do_div(delta, bm_cnt);
	avg = delta;

	if (stddev > 0) {
		int i = 0;
		/*
		 * stddev is the square of standard deviation but
		 * we want the actualy number. Use the average
		 * as our seed to find the std.
		 *
		 * The next try is:
		 *  x = (x + N/x) / 2
		 *
		 * Where N is the squared number to find the square
		 * root of.
		 */
		seed = avg;
		do {
			last_seed = seed;
			seed = stddev;
			if (!last_seed)
				break;
			do_div(seed, last_seed);
			seed += last_seed;
			do_div(seed, 2);
		} while (i++ < 10 && last_seed != seed);

		std = seed;
	}

	scnprintf(bm_str, BENCHMARK_EVENT_STRLEN,
		  "last=%llu first=%llu max=%llu min=%llu avg=%u std=%d std^2=%lld",
		  bm_last, bm_first, bm_max, bm_min, avg, std, stddev);
}

static int benchmark_event_kthread(void *arg)
{
	/* sleep a bit to make sure the tracepoint gets activated */
	msleep(100);

	while (!kthread_should_stop()) {

		trace_do_benchmark();

		/*
		 * We don't go to sleep, but let others
		 * run as well.
		 */
		cond_resched();
	}

	return 0;
}

/*
 * When the benchmark tracepoint is enabled, it calls this
 * function and the thread that calls the tracepoint is created.
 */
void trace_benchmark_reg(void)
{
	bm_event_thread = kthread_run(benchmark_event_kthread,
				      NULL, "event_benchmark");
	WARN_ON(!bm_event_thread);
}

/*
 * When the benchmark tracepoint is disabled, it calls this
 * function and the thread that calls the tracepoint is deleted
 * and all the numbers are reset.
 */
void trace_benchmark_unreg(void)
{
	if (!bm_event_thread)
		return;

	kthread_stop(bm_event_thread);

	strcpy(bm_str, "START");
	bm_total = 0;
	bm_totalsq = 0;
	bm_last = 0;
	bm_max = 0;
	bm_min = 0;
	bm_cnt = 0;
	/* bm_first doesn't need to be reset but reset it anyway */
	bm_first = 0;
}
