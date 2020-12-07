/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2020 NetApp, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Tracing during booting and shutdown sequence.
 * A tool to determine the amount of time spent in various phases of
 * boot and shutdown.
 *
 * A trace buffer consists of a header and N event slots. Buffers can
 * be linked together to extend the size and increase the number of
 * events traced.
 *
 * stats are displayed via "sysctl sysvar.boottimes"
 *
 * Atomic operations are used to increment the array index.  Reading
 * the trace output is lockless as well.  This is because the asup
 * wants to read the output repeatedly during boot and printing the
 * output is very slow.  Having a lock for this case would mean boot
 * events would block when the array is being read; this would perturb
 * the performance measurements for boot time events, and give a false
 * indication that some events are taking a long time to complete when
 * in fact they are blocked because someone is reading the trace table.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/boottrace.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sbuf.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>

#include <machine/clock.h>

#ifdef NETAPP
#include <netapp/sys/sys/sk_proc_private.h>
#include <netapp/sys/sys/boot_time_capture.h>
#include <bsd_plat/watchdog.h>

#include <coredump/dumptriage.h>
#endif

static MALLOC_DEFINE(M_BOOTTRACE, "boottrace", "memory for boot tracing");

#define BT_TABLE_DEFSIZE   3000
#define BT_TABLE_RUNSIZE   2000
#define BT_TABLE_SHTSIZE   1000
#define BT_TABLE_MINSIZE    500
#define BT_EVENT_NAMELEN     40
#define BT_EVENT_TDNAMELEN   24

#if 0
/* Dump Triage definitions */
#define DT_BOOTTRACE_SUBTYPE "SW/kernel"
#define DT_BOOTTRACE_DESCRIPTION "boot-time tracing, run-time tracing, & shutdown-time tracing"

bool is_dump_triage = false;
#endif

/*
 * Boot-time & shutdown-time event.
 */
struct bt_event {
	uint64_t tsc;                        /* CPU TSC */
	uint64_t tick;                       /* kernel tick */
	uint32_t cpuid;                      /* cpu on which the event ran */
	uint32_t cputime;                    /* Microseconds of process CPU time */
	uint32_t inblock;                    /* # of blocks in */
	uint32_t oublock;                    /* # of blocks out */
	pid_t	 pid;                        /* Current PID */
	char     name[BT_EVENT_NAMELEN];     /* event name */
	char     tdname[BT_EVENT_TDNAMELEN]; /* thread name */
};

struct bt {
	uint32_t size;		/* Trace table size */
	uint32_t curr;		/* Trace entry to use */
	uint32_t wrap;		/* Wrap-around, instead of dropping */
	uint32_t drops_early;	/* Trace entries dropped before init */
	uint32_t drops_full;	/* Trace entries dropped after full */
	struct bt_event *table;
};

/* Boot-time tracing */
static struct bt bt;

/* Run-time tracing */
static struct bt rt;

/* Shutdown-time tracing */
static struct bt st;

/* Set when system boot is complete */
static bool bootdone;

/* Set when system shutdown is started */
static bool shutdown;

/* Turn on tracing to console */
static bool dotrace_debugging;

#ifdef NETAPP
/* Dump Triage */
static void boottrace_dump_triage_hook(const void*);
#endif

static int sysctl_boottrace(SYSCTL_HANDLER_ARGS);
static int sysctl_runtrace(SYSCTL_HANDLER_ARGS);
static int sysctl_shuttrace(SYSCTL_HANDLER_ARGS);
static int sysctl_boottrace_reset(SYSCTL_HANDLER_ARGS);

/*
 * Dump a trace to buffer or if buffer is NULL to console.
 *
 * Non-zero delta_threshold selectively prints entries based on delta
 * with current and previous entry. Otherwise, delta_threshold of 0
 * prints every trace entry and delta.
 *
 * Output something like this:
 *
 * CPU      msecs      delta process                  event
 *  11 1228262715          0 init                     shutdown pre sync begin
 *   3 1228265622       2907 init                     shutdown pre sync complete
 *   3 1228265623          0 init                     shutdown turned swap off
 *  18 1228266466        843 init                     shutdown unmounted all filesystems
 *
 * How to interpret:
 *
 * delta column represents the time in milliseconds between this event and the previous.
 * Usually that means you can take the previous event, current event, match them
 * up in the code, and whatever lies between is the culprit taking time.
 *
 * For example, above: Pre-Sync is taking 2907ms, and something between swap and unmount
 * filesystems is taking 843 seconds.
 *
 * An event with a delta of 0 are 'landmark' events that simply exist in the output
 * for the developer to know where the time measurement begins. The 0 is an arbitrary
 * number that can effectively be ignored.
 */
#define BTC_DELTA_PRINT(bte, msecs, delta) do {					\
	if (sbp) {								\
		sbuf_printf(sbp, fmt, (bte)->cpuid, msecs, delta, (bte)->tdname,	\
			    (bte)->name, (bte)->pid, (bte)->cputime / 1000000,	\
			    ((bte)->cputime % 1000000) / 10000,			\
			    (bte)->inblock, (bte)->oublock);			\
	} else {								\
			printf(fmt, (bte)->cpuid, msecs, delta, (bte)->tdname, (bte)->name,	\
			       (bte)->pid, (bte)->cputime / 1000000,			\
			       ((bte)->cputime % 1000000) / 10000,			\
			       (bte)->inblock, (bte)->oublock);				\
	}										\
} while (0)

#if 0
		if (is_dump_triage) {							\
			dt_printf(DT_LEVEL2, fmt, (bte)->cpuid, msecs, delta, (bte)->tdname, (bte)->name,	\
			          (bte)->pid, (bte)->cputime / 1000000,			\
			          ((bte)->cputime % 1000000) / 10000,			\
			          (bte)->inblock, (bte)->oublock);			\
		} else {								\
		}									\
	}									\

#endif

static void
boottrace_display(struct sbuf *sbp, struct bt *btp, uint64_t dthres)
{
	struct bt_event *evtp;
	struct bt_event *last_evtp;
	uint64_t msecs;
	uint64_t first_msecs;
	uint64_t last_msecs;
	uint64_t dmsecs;
	uint64_t last_dmsecs;
	uint64_t total_dmsecs;
	uint32_t i;
	uint32_t curr;
	const char *fmt     = "%3u %10llu %10llu %-24s %-40s %5d %4d.%02d %5u %5u\n";
	const char *hdr_fmt = "\n\n%3s %10s %10s %-24s %-40s %5s %6s %5s %5s\n";
	bool printed;
	bool last_printed;

	if (sbp) {
		sbuf_printf(sbp, hdr_fmt,
			    "CPU", "msecs", "delta", "process",
			    "event", "PID", "CPUtime", "IBlks", "OBlks");
	} else {
#if 0
		if (is_dump_triage) {
			dt_printf(DT_LEVEL2, hdr_fmt,
			          "CPU", "msecs", "delta", "process",
			          "event", "PID", "CPUtime", "IBlks", "OBlks");
		} else {
#endif
		printf(hdr_fmt,
		       "CPU", "msecs", "delta", "process",
		       "event", "PID", "CPUtime", "IBlks", "OBlks");
	}
	first_msecs = 0;
	last_evtp = NULL;
	last_msecs = 0;
	last_dmsecs = 0;
	last_printed = false;
	i = curr = btp->curr;
	do {
		evtp = &btp->table[i];
		if (evtp->tsc == 0) {
			goto next;
		}
		msecs = evtp->tsc * 1000 / tsc_freq;
		dmsecs = (last_msecs && msecs > last_msecs)?  msecs - last_msecs : 0;
		printed = false;
		/*
		 * If a threshold is defined, start filtering events
		 * by the delta of msecs.
		 */
		if (dthres && (dmsecs > dthres)) {
			/*
			 * Print the previous entry as a landmark,
			 * even if it falls below the threshold.
			 */
			if (last_evtp && !last_printed) {
				BTC_DELTA_PRINT(last_evtp, last_msecs, last_dmsecs);
			}
			BTC_DELTA_PRINT(evtp, msecs, dmsecs);
			printed = true;
		} else {
			if (dthres == 0) {
				BTC_DELTA_PRINT(evtp, msecs, dmsecs);
				printed = true;
			}
		}
		if (first_msecs == 0 || msecs < first_msecs) {
			first_msecs = msecs;
		}
		last_evtp = evtp;
		last_msecs = msecs;
		last_dmsecs = dmsecs;
		last_printed = printed;
		maybe_yield();
		//maybe_yield_pri(PRI_UNCHANGED);
next:
		i = (i + 1) % btp->size;
	} while (i != curr);
	total_dmsecs = (last_msecs > first_msecs)? last_msecs - first_msecs : 0;
	if (sbp) {
		sbuf_printf(sbp, "Total measured time: %lu msecs\n", total_dmsecs);
	} else {
#if 0
		if (is_dump_triage) {
			dt_printf(DT_LEVEL2, "Total measured time: %lu msecs\n", total_dmsecs);
		} else {
			printf("Total measured time: %lu msecs\n", total_dmsecs);
		}
#endif
		printf("Total measured time: %lu msecs\n", total_dmsecs);
	}
}

SYSCTL_NODE(_kern, OID_AUTO, boottrace, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "boottrace statistics");
SYSCTL_PROC(_kern_boottrace, OID_AUTO, boottimes,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE | CTLFLAG_SKIP,
    0, 0, sysctl_boottrace, "A",
    "boot-time tracing");
SYSCTL_PROC(_kern_boottrace, OID_AUTO, runtimes,
    CTLTYPE_STRING | CTLFLAG_WR | CTLFLAG_MPSAFE,
    0, 0, sysctl_runtrace, "A",
    "run-time tracing");
SYSCTL_PROC(_kern_boottrace, OID_AUTO, shuttimes,
    CTLTYPE_STRING | CTLFLAG_WR | CTLFLAG_MPSAFE,
    0, 0, sysctl_shuttrace, "A",
    "shutdown-time tracing");
SYSCTL_PROC(_kern_boottrace, OID_AUTO, reset,
    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    0, 0, sysctl_boottrace_reset, "A",
    "boot-time tracing reset");

//SYSCTL_DECL(_sysvar);
//SYSCTL_ROOT_NODE(OID_AUTO, sysvar, CTLFLAG_RW, 0, "boottrace statistics");

/*
 * Enable shutdown tracing console dump.
 *
 * 0 - Disabled
 * 1 - Enabled
 */
int shutdown_trace;
SYSCTL_INT(_kern_boottrace, OID_AUTO, shutdown_trace, CTLFLAG_RWTUN,
	   &shutdown_trace, 0, "Enable kernel shutdown tracing to the console.");
/*
 * What is the threshold (ms) below which events are ignored, used in
 * determining what to dump to the console.
 */
static int shutdown_trace_threshold;
SYSCTL_INT(_kern_boottrace, OID_AUTO, shutdown_trace_threshold, CTLFLAG_RW,
	   &shutdown_trace_threshold, 0, "Tracing threshold (ms) below which tracing is ignored.");

/*
 * API to dump either boottrace or shuttrace entries to the console,
 * given a delta threshold.
 */
void
boottrace_dump_console(void)
{
	if (!shutdown_trace) {
		return;
	}
	//if (shutdown || shutdown_clean || rebooting || panicstr || panic_count) {
	if (shutdown || rebooting || panicstr) {
		boottrace_display(NULL, &st, shutdown_trace_threshold);
	} else {
		boottrace_display(NULL, &bt, 0);
		boottrace_display(NULL, &rt, 0);
	}
}

static int
dotrace(struct bt *btp, const char *eventname, const char *tdname)
{
	uint32_t idx, nxt;
	struct rusage usage;
	static bool bootarg_needs_init = true;

	if (bootarg_needs_init) {
		dotrace_debugging = getenv_is_true("bootarg.print_dotrace");
		bootarg_needs_init = false;
	}
	if (!tdname) {
		tdname = (curproc->p_flag & P_SYSTEM)? curthread->td_name : curproc->p_comm;
	}
	if (dotrace_debugging) {
		printf("dotrace[");
		printf("cpu=%u, pid=%d, tsc=%lu, tick=%d, td='%s', event='%s'",
				PCPU_GET(cpuid),
				curthread->td_proc->p_pid,
				get_cyclecount(),
				ticks,
				tdname,
				eventname);
	}
	if (btp->table == NULL) {
		btp->drops_early++;
		if (dotrace_debugging) {
			printf(", return=ENOSPC_1]\n");
		}
		return (ENOSPC);
	}
	do {
		idx = btp->curr;
		nxt = (idx + 1) % btp->size;
		if (nxt == 0 && btp->wrap == 0) {
			btp->drops_full++;
			if (dotrace_debugging) {
				printf(", return=ENOSPC_2]\n");
			}
			return (ENOSPC);
		}
	} while (!atomic_cmpset_int(&btp->curr, idx, nxt));
	btp->table[idx].cpuid = PCPU_GET(cpuid);
	btp->table[idx].tsc   = get_cyclecount(),
	btp->table[idx].tick  = ticks;
	btp->table[idx].pid   = curthread->td_proc->p_pid;
	/*
	 * Don't try to get CPU time for the kernel proc0
	 * or for critical section activities.
	 */
	if ((curthread->td_proc == &proc0) || (curthread->td_critnest != 0)) {
		btp->table[idx].cputime = 0;
		btp->table[idx].inblock = 0;
		btp->table[idx].oublock = 0;
	} else {
		/* Precheck disable/enable to be removed via BURT 1070574 */
		//ONTAP_NOSWITCH_PRECHECK_DISABLE;
		kern_getrusage(curthread, RUSAGE_SELF, &usage);
		//ONTAP_NOSWITCH_PRECHECK_ENABLE;
		btp->table[idx].cputime = (uint32_t) (usage.ru_utime.tv_sec * 1000000 +
					  usage.ru_utime.tv_usec +
					  usage.ru_stime.tv_sec * 1000000 +
					  usage.ru_stime.tv_usec);
		btp->table[idx].inblock = (uint32_t) usage.ru_inblock;
		btp->table[idx].oublock = (uint32_t) usage.ru_oublock;
	}
	strlcpy(btp->table[idx].name, eventname, BT_EVENT_NAMELEN);
	strlcpy(btp->table[idx].tdname, tdname, BT_EVENT_TDNAMELEN);
	if (dotrace_debugging) {
		printf(", return=0]\n");
	}
	return (0);
}

/*
 * Log various boot-time events.
 *
 * We don't use a lock because we want this to be callable from interrupt
 * context.
 */
int
boottrace(const char *eventname, const char *tdname)
{
	struct bt *trace;

	trace = &bt;
	if (bootdone) {
		trace = &rt;
	}
//	if (shutdown || shutdown_clean || rebooting || panicstr || panic_count) {
	if (shutdown || rebooting || panicstr) {
		trace = &st;
	}
	return (dotrace(trace, eventname, tdname));
}

/*
 * Log a run-time event & switch over to run-time tracing mode.
 */
static int
runtrace(const char *eventname, const char *tdname)
{
	int error;

	error = boottrace(eventname, tdname);
	bootdone = true;
	return (error);
}

/*
 * Log a shutdown-time event & switch over to shutdown tracing mode.
 */
int
shuttrace(const char *eventname, const char *tdname)
{
	shutdown = true;
	return (dotrace(&st, eventname, tdname));
}

/*
 * The input from userspace must have a : in order for us to parse it.
 * Format is <tdname>:<eventname>
 * e.g. reboot(8):SIGINT to init(8)...
 */
static void
boottrace_parse_message(char *message, char **eventname, char **tdname)
{
	char *delim;

	delim = strchr(message, ':');
	if (delim) {
		*delim = '\0';
		*tdname = message;
		*eventname = ++delim;
	} else {
		*tdname = curproc->p_comm;
		*eventname = message;
	}
}

static int
sysctl_boottrace(SYSCTL_HANDLER_ARGS)
{
	int error;
	char *eventname;
	char *tdname;
	struct sbuf *sbuf;
	char message[BT_EVENT_TDNAMELEN + 1 + BT_EVENT_NAMELEN];

	/*
	 * Check to see if we are creating a new entry in the table
	 * or dumping the output we've already created.
	 */
	if (!req->newptr) {
		sbuf = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
		boottrace_display(sbuf, &bt, 0);
		boottrace_display(sbuf, &rt, 0);
		sbuf_finish(sbuf);
		SYSCTL_OUT(req, sbuf_data(sbuf), sbuf_len(sbuf));
		sbuf_delete(sbuf);
		return 0;
	}

	message[0] = '\0';
	error = sysctl_handle_string(oidp, message, sizeof(message), req);
	if (error) {
		return error;
	}
	boottrace_parse_message(message, &eventname, &tdname);
	error = boottrace(eventname, tdname);
	if (error == ENOSPC) {
		/* Ignore table full error (BURT 416249) */
		error = 0;
	}
	return error;
}

static int
sysctl_runtrace(SYSCTL_HANDLER_ARGS)
{
	int error;
	char *eventname;
	char *tdname;
	char message[BT_EVENT_TDNAMELEN + 1 + BT_EVENT_NAMELEN];

	/* No output */
	if (!req->newptr) {
		return 0;
	}

	message[0] = '\0';
	error = sysctl_handle_string(oidp, message, sizeof(message), req);
	if (error) {
		return error;
	}
	boottrace_parse_message(message, &eventname, &tdname);
	error = runtrace(eventname, tdname);
	if (error == ENOSPC) {
		/* Ignore table full error (BURT 416249) */
		error = 0;
	}
	return error;
}

static int
sysctl_shuttrace(SYSCTL_HANDLER_ARGS)
{
	int error;
	char *eventname;
	char *tdname;
	char message[BT_EVENT_TDNAMELEN + 1 + BT_EVENT_NAMELEN];

	/* No output */
	if (!req->newptr) {
		return 0;
	}

	message[0] = '\0';
	error = sysctl_handle_string(oidp, message, sizeof(message), req);
	if (error) {
		return error;
	}
	boottrace_parse_message(message, &eventname, &tdname);
	error = shuttrace(eventname, tdname);
	if (error == ENOSPC) {
		/* Ignore table full error (BURT 416249) */
		error = 0;
	}
	return error;
}

/*
 * Start run-time tracing, if it is not already active.
 */
void
boottrace_reset(const char *actor)
{
	char tmpbuf[64];

	snprintf(tmpbuf, sizeof(tmpbuf), "reset: %s", actor);
	runtrace(tmpbuf, NULL);
}

/*
 * Note that a resize implies a reset, i.e., the index is reset to 0.
 * We never shrink the array; we can only increase its size.
 */
int
boottrace_resize(uint32_t newsize)
{
	if (newsize <= rt.size) {
		return (EINVAL);
	}
	rt.table = realloc(rt.table, newsize * sizeof(struct bt_event),
	    M_BOOTTRACE, M_WAITOK | M_ZERO);
	if (rt.table == NULL)
		return (ENOMEM);

	rt.size = newsize;
	boottrace_reset("boottrace_resize");
	return (0);
}

static int
sysctl_boottrace_reset(SYSCTL_HANDLER_ARGS)
{
	if (req->newptr) {
		boottrace_reset("sysctl_boottrace_reset");
	}
	return (0);
}

#if 0
static void
boottrace_dump_triage_hook(const void* args)
{
	is_dump_triage = true;

	dt_printf(DT_LEVEL2, "Boot-time tracing");
	boottrace_display(NULL, &bt, 0);
	dt_printf(DT_LEVEL2, "\n");

	dt_printf(DT_LEVEL2, "Run-time tracing");
	boottrace_display(NULL, &rt, 0);
	dt_printf(DT_LEVEL2, "\n");

	dt_printf(DT_LEVEL2, "Shutdown-time tracing");
	boottrace_display(NULL, &st, shutdown_trace_threshold);
	dt_printf(DT_LEVEL2, "\n");
}
DT_DEFINE_GENERIC(DT_BOOTTRACE_GENERIC,
                  DT_BOOTTRACE_SUBTYPE,
                  DT_BOOTTRACE_DESCRIPTION,
                  boottrace_dump_triage_hook,
                  NULL);
#endif

static void
boottrace_init(void)
{
	/* Boottime trace table */
	bt.size = BT_TABLE_DEFSIZE;
	TUNABLE_INT_FETCH("boottrace-table-size", &bt.size);
	bt.size = max(bt.size, BT_TABLE_MINSIZE);
	bt.table = malloc(bt.size * sizeof(struct bt_event), M_BOOTTRACE, M_WAITOK|M_ZERO);

	/* Stick in initial entry with machdep times */	
	bt.table[0].cpuid   = PCPU_GET(cpuid);
	bt.table[0].tsc     = 0;
	bt.table[0].tick    = 0;
	bt.table[0].cputime = 0;
	bt.table[0].inblock = 0;
	bt.table[0].oublock = 0;
	strlcpy(bt.table[0].tdname, "boottime", BT_EVENT_TDNAMELEN);
	strlcpy(bt.table[0].name, "initial event", BT_EVENT_NAMELEN);
	bt.curr = 1;

	/* Run-time trace table (may wrap-around) */
	rt.wrap = 1;
	rt.size = BT_TABLE_RUNSIZE;
	rt.table = malloc(rt.size * sizeof(struct bt_event), M_BOOTTRACE, M_WAITOK|M_ZERO);

	/* Shutdown trace table */
	st.size = BT_TABLE_SHTSIZE;
	st.table = malloc(st.size * sizeof(struct bt_event), M_BOOTTRACE, M_WAITOK|M_ZERO);
}
SYSINIT(boottrace, SI_SUB_CPU, SI_ORDER_FIRST, boottrace_init, 0);
