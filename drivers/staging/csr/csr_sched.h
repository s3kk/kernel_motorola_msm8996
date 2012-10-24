#ifndef CSR_SCHED_H__
#define CSR_SCHED_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/
#include <linux/types.h>
#include "csr_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An identifier issued by the scheduler. */
typedef u32 CsrSchedIdentifier;

/* A task identifier */
typedef u16 CsrSchedTaskId;

/* A queue identifier */
typedef u16 CsrSchedQid;
#define CSR_SCHED_QID_INVALID     ((CsrSchedQid) 0xFFFF)

/* A message identifier */
typedef CsrSchedIdentifier CsrSchedMsgId;

/* A timer event identifier */
typedef CsrSchedIdentifier CsrSchedTid;
#define CSR_SCHED_TID_INVALID     ((CsrSchedTid) 0)

/* Scheduler entry functions share this structure */
typedef void (*schedEntryFunction_t)(void **inst);

/* Time constants. */
#define CSR_SCHED_TIME_MAX                (0xFFFFFFFF)
#define CSR_SCHED_MILLISECOND             (1000)
#define CSR_SCHED_SECOND                  (1000 * CSR_SCHED_MILLISECOND)
#define CSR_SCHED_MINUTE                  (60 * CSR_SCHED_SECOND)

/* Queue and primitive that identifies the environment */
#define CSR_SCHED_TASK_ID        0xFFFF
#define CSR_SCHED_PRIM                   (CSR_SCHED_TASK_ID)
#define CSR_SCHED_EXCLUDED_MODULE_QUEUE      0xFFFF

/*
 * Background interrupt definitions
 */
typedef u16 CsrSchedBgint;
#define CSR_SCHED_BGINT_INVALID ((CsrSchedBgint) 0xFFFF)

typedef void (*CsrSchedBgintHandler)(void *);

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSchedMessagePut
 *
 *  DESCRIPTION
 *      Sends a message consisting of the integer "mi" and the void * pointer
 *      "mv" to the message queue "q".
 *
 *      "mi" and "mv" are neither inspected nor changed by the scheduler - the
 *      task that owns "q" is expected to make sense of the values. "mv" may
 *      be null.
 *
 *  NOTE
 *      If "mv" is not null then it will typically be a chunk of kmalloc()ed
 *      memory, though there is no need for it to be so. Tasks should normally
 *      obey the convention that when a message built with kmalloc()ed memory
 *      is given to CsrSchedMessagePut() then ownership of the memory is ceded to the
 *      scheduler - and eventually to the recipient task. I.e., the receiver of
 *      the message will be expected to kfree() the message storage.
 *
 *  RETURNS
 *      void.
 *
 *----------------------------------------------------------------------------*/
#if defined(CSR_LOG_ENABLE) && defined(CSR_LOG_INCLUDE_FILE_NAME_AND_LINE_NUMBER)
void CsrSchedMessagePutStringLog(CsrSchedQid q,
    u16 mi,
    void *mv,
    u32 line,
    const char *file);
#define CsrSchedMessagePut(q, mi, mv) CsrSchedMessagePutStringLog((q), (mi), (mv), __LINE__, __FILE__)
#else
void CsrSchedMessagePut(CsrSchedQid q,
    u16 mi,
    void *mv);
#endif

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSchedMessageBroadcast
 *
 *  DESCRIPTION
 *      Sends a message to all tasks.
 *
 *      The user must supply a "factory function" that is called once
 *      for every task that exists. The "factory function", msg_build_func,
 *      must allocate and initialise the message and set the msg_build_ptr
 *      to point to the message when done.
 *
 *  NOTE
 *      N/A
 *
 *  RETURNS
 *      void
 *
 *----------------------------------------------------------------------------*/
#if defined(CSR_LOG_ENABLE) && defined(CSR_LOG_INCLUDE_FILE_NAME_AND_LINE_NUMBER)
void CsrSchedMessageBroadcastStringLog(u16 mi,
    void *(*msg_build_func)(void *),
    void *msg_build_ptr,
    u32 line,
    const char *file);
#define CsrSchedMessageBroadcast(mi, fn, ptr) CsrSchedMessageBroadcastStringLog((mi), (fn), (ptr), __LINE__, __FILE__)
#else
void CsrSchedMessageBroadcast(u16 mi,
    void *(*msg_build_func)(void *),
    void *msg_build_ptr);
#endif

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSchedTimerSet
 *
 *  DESCRIPTION
 *      Causes the void function "fn" to be called with the arguments
 *      "fniarg" and "fnvarg" after "delay" has elapsed.
 *
 *      "delay" must be less than half the range of a CsrTime.
 *
 *      CsrSchedTimerSet() does nothing with "fniarg" and "fnvarg" except
 *      deliver them via a call to "fn()".   (Unless CsrSchedTimerCancel()
 *      is used to prevent delivery.)
 *
 *  NOTE
 *      The function will be called at or after "delay"; the actual delay will
 *      depend on the timing behaviour of the scheduler's tasks.
 *
 *  RETURNS
 *      CsrSchedTid - A timed event identifier, can be used in CsrSchedTimerCancel().
 *
 *----------------------------------------------------------------------------*/
#if defined(CSR_LOG_ENABLE) && defined(CSR_LOG_INCLUDE_FILE_NAME_AND_LINE_NUMBER)
CsrSchedTid CsrSchedTimerSetStringLog(u32 delay,
    void (*fn)(u16 mi, void *mv),
    u16 fniarg,
    void *fnvarg,
    u32 line,
    const char *file);
#define CsrSchedTimerSet(d, fn, fni, fnv) CsrSchedTimerSetStringLog((d), (fn), (fni), (fnv), __LINE__, __FILE__)
#else
CsrSchedTid CsrSchedTimerSet(u32 delay,
    void (*fn)(u16 mi, void *mv),
    u16 fniarg,
    void *fnvarg);
#endif

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSchedTimerCancel
 *
 *  DESCRIPTION
 *      Attempts to prevent the timed event with identifier "eventid" from
 *      occurring.
 *
 *  RETURNS
 *      u8 - TRUE if cancelled, FALSE if the event has already occurred.
 *
 *----------------------------------------------------------------------------*/
#if defined(CSR_LOG_ENABLE) && defined(CSR_LOG_INCLUDE_FILE_NAME_AND_LINE_NUMBER)
u8 CsrSchedTimerCancelStringLog(CsrSchedTid eventid,
    u16 *pmi,
    void **pmv,
    u32 line,
    const char *file);
#define CsrSchedTimerCancel(e, pmi, pmv) CsrSchedTimerCancelStringLog((e), (pmi), (pmv), __LINE__, __FILE__)
#else
u8 CsrSchedTimerCancel(CsrSchedTid eventid,
    u16 *pmi,
    void **pmv);
#endif

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrSchedTaskQueueGet
 *
 *  DESCRIPTION
 *      Return the queue identifier for the currently running queue
 *
 *  RETURNS
 *      CsrSchedQid - The current task queue identifier, or 0xFFFF if not available.
 *
 *----------------------------------------------------------------------------*/
CsrSchedQid CsrSchedTaskQueueGet(void);


#ifdef __cplusplus
}
#endif

#endif
