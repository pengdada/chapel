//
// Pthread implementation of Chapel threading interface
//

#ifdef __OPTIMIZE__
// Turn assert() into a no op if the C compiler defines the macro above.
#define NDEBUG
#endif

#include "chplcomm.h"
#include "chplexit.h"
#include "chplmem.h"
#include "chplrt.h"
#include "chplthreads.h"
#include "error.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>
#include <inttypes.h>

//
// task pool: linked list of tasks
//
typedef struct chpl_pool_struct* chpl_task_pool_p;
typedef struct chpl_pool_struct {
  chpl_threadfp_t  fun;          // function to call for task
  chpl_threadarg_t arg;          // argument to the function
  _Bool            serial_state; // whether new threads can be created while executing fun
  _Bool            begun;        // whether execution of this task has begun
  chpl_task_list_p task_list;    // points to the (cobegin) task list entry, if there is one
  chpl_task_pool_p next;
} task_pool_t;

// This struct is intended for use in a circular linked list where the pointer
// to the list actually points to the tail of the list, i.e., the last entry
// inserted into the list, making it easier to append items to the end of the list.
// Since it is part of a circular list, the last entry will, of course,
// point to the first entry in the list.
struct chpl_task_list {
  chpl_threadfp_t fun;
  chpl_threadarg_t arg;
  chpl_task_pool_p task_pool_entry;
  chpl_task_list_p next;
  _Bool completed;  // whether execution of the associated task has finished
};


static chpl_mutex_t     threading_lock; // critical section lock
static chpl_condvar_t   wakeup_signal;  // signal a waiting thread
static pthread_key_t    serial_key;     // per-thread serial state
static chpl_task_pool_p task_pool_head; // head of task pool
static chpl_task_pool_p task_pool_tail; // tail of task pool
static int              waking_cnt;     // number of threads signaled to wakeup
static int              running_cnt;    // number of running threads 
static int              threads_cnt;    // number of threads (total)
static chpl_mutex_t     report_lock;    // critical section lock
static pthread_key_t    lock_report_key;


typedef struct _lockReport {
  const char* filename;
  int lineno;
  int maybeLocked;
  struct _lockReport* next;
} lockReport;


lockReport* lockReportHead = NULL;
lockReport* lockReportTail = NULL;

static void traverseLockedThreads(int sig);
static void setBlockingLocation(int lineno, _string filename);
static void unsetBlockingLocation(void);
static void initializeLockReportForThread(void);
static _string idleThreadName = "|idle|";

// Condition variables

static chpl_condvar_p chpl_condvar_new(void) {
  chpl_condvar_p cv;
  cv = (chpl_condvar_p) chpl_alloc(sizeof(chpl_condvar_t), "condition var", 0, 0);
  if (pthread_cond_init(cv, NULL))
    chpl_internal_error("pthread_cond_init() failed");
  return cv;
}


// Mutex

static void chpl_mutex_init(chpl_mutex_p mutex) {
  // WAW: how to explicitly specify blocking-type?
  if (pthread_mutex_init(mutex, NULL))
    chpl_internal_error("pthread_mutex_init() failed");
}

static chpl_mutex_p chpl_mutex_new(void) {
  chpl_mutex_p m;
  m = (chpl_mutex_p) chpl_alloc(sizeof(chpl_mutex_t), "mutex", 0, 0);
  chpl_mutex_init(m);
  return m;
}

int chpl_mutex_lock(chpl_mutex_p mutex) {
  int return_value;
  if ((return_value = pthread_mutex_lock(mutex)))
    chpl_internal_error("pthread_mutex_lock() failed");
  return return_value;
}

void chpl_mutex_unlock(chpl_mutex_p mutex) {
  if (pthread_mutex_unlock(mutex))
    chpl_internal_error("pthread_mutex_unlock() failed");
}


// Sync variables

int chpl_sync_lock(chpl_sync_aux_t *s) {
  return chpl_mutex_lock(s->lock);
}

void chpl_sync_unlock(chpl_sync_aux_t *s) {
  chpl_mutex_unlock(s->lock);
}

int chpl_sync_wait_full_and_lock(chpl_sync_aux_t *s, int32_t lineno, _string filename) {
  int return_value;
  setBlockingLocation(lineno, filename);
  return_value = chpl_mutex_lock(s->lock);
  while (return_value == 0 && !s->is_full) {
    if ((return_value = pthread_cond_wait(s->signal_full, s->lock)))
      chpl_internal_error("pthread_cond_wait() failed");
  }
  unsetBlockingLocation();
  return return_value;
}

int chpl_sync_wait_empty_and_lock(chpl_sync_aux_t *s, int32_t lineno, _string filename) {
  int return_value;
  setBlockingLocation(lineno, filename);
  return_value = chpl_mutex_lock(s->lock);
  while (return_value == 0 && s->is_full) {
    if ((return_value = pthread_cond_wait(s->signal_empty, s->lock)))
      chpl_internal_error("pthread_cond_wait() failed");
  }
  unsetBlockingLocation();
  return return_value;
}

void chpl_sync_mark_and_signal_full(chpl_sync_aux_t *s) {
  s->is_full = true;
  chpl_sync_unlock(s);
  if (pthread_cond_signal(s->signal_full))
    chpl_internal_error("pthread_cond_signal() failed");
}

void chpl_sync_mark_and_signal_empty(chpl_sync_aux_t *s) {
  s->is_full = false;
  chpl_sync_unlock(s);
  if (pthread_cond_signal(s->signal_empty))
    chpl_internal_error("pthread_cond_signal() failed");
}

chpl_bool chpl_sync_is_full(void *val_ptr, chpl_sync_aux_t *s, chpl_bool simple_sync_var) {
  return s->is_full;
}

void chpl_init_sync_aux(chpl_sync_aux_t *s) {
  s->is_full = false;
  s->lock = chpl_mutex_new();
  s->signal_full = chpl_condvar_new();
  s->signal_empty = chpl_condvar_new();
}


// Single variables

int chpl_single_lock(chpl_single_aux_t *s) {
  return chpl_mutex_lock(s->lock);
}

void chpl_single_unlock(chpl_single_aux_t *s) {
  chpl_mutex_unlock(s->lock);
}

int chpl_single_wait_full(chpl_single_aux_t *s, int32_t lineno, _string filename) {
  int return_value;
  setBlockingLocation(lineno, filename);
  return_value = chpl_mutex_lock(s->lock);
  while (return_value == 0 && !s->is_full) {
    if ((return_value = pthread_cond_wait(s->signal_full, s->lock)))
      chpl_internal_error("invalid mutex in chpl_single_wait_full");
  }
  unsetBlockingLocation();
  return return_value;
}

void chpl_single_mark_and_signal_full(chpl_single_aux_t *s) {
  s->is_full = true;
  chpl_mutex_unlock(s->lock);
  if (pthread_cond_signal(s->signal_full))
    chpl_internal_error("pthread_cond_signal() failed");
}

chpl_bool chpl_single_is_full(void *val_ptr, chpl_single_aux_t *s, chpl_bool simple_single_var) {
  return s->is_full;
}

void chpl_init_single_aux(chpl_single_aux_t *s) {
  s->is_full = false;
  s->lock = chpl_mutex_new();
  s->signal_full = chpl_condvar_new();
}


// Threads

static void serial_delete(_Bool *p) {
  if (p != NULL) {
    chpl_free(p, 0, 0);
  }
}

int32_t chpl_threads_getMaxThreads(void) { return 0; }
int32_t chpl_threads_maxThreadsLimit(void) { return 0; }

void initChplThreads() {
  chpl_mutex_init(&threading_lock);
  if (pthread_cond_init(&wakeup_signal, NULL))
    chpl_internal_error("pthread_cond_init() failed in");
  running_cnt = 0;                     // only main thread running
  waking_cnt = 0;
  threads_cnt = 0;
  task_pool_head = task_pool_tail = NULL;

  chpl_mutex_init(&_memtrack_lock);
  chpl_mutex_init(&_memstat_lock);
  chpl_mutex_init(&_memtrace_lock);
  chpl_mutex_init(&_malloc_lock);

  if (pthread_key_create(&serial_key, (void(*)(void*))serial_delete))
    chpl_internal_error("serial key not created");
  if (pthread_key_create(&lock_report_key, NULL))
    chpl_internal_error("lock report key not created");

  if (blockreport) {
    chpl_mutex_init(&report_lock);
    signal(SIGINT, traverseLockedThreads);
  }

  chpl_thread_init();
}


void exitChplThreads() {
  _Bool debug = false;
  if (debug)
    fprintf(stderr, "A total of %d threads were created; waking_cnt = %d\n", threads_cnt, waking_cnt);
  pthread_key_delete(serial_key);
}


void chpl_thread_init(void) {
  if (blockreport)
    initializeLockReportForThread();
}


uint64_t chpl_thread_id(void) {
  return (intptr_t) pthread_self();
}


chpl_bool chpl_get_serial(void) {
  _Bool *p;
  p = (_Bool*) pthread_getspecific(serial_key);
  return p == NULL ? false : *p;
}

void chpl_set_serial(chpl_bool state) {
  _Bool *p;
  p = (_Bool*) pthread_getspecific(serial_key);
  if (p == NULL) {
    if (state) {
      p = (_Bool*) chpl_alloc(sizeof(_Bool), "serial flag", 0, 0);
      *p = state;
      if (pthread_setspecific(serial_key, p)) {
        if (pthread_key_create(&serial_key, (void(*)(void*))serial_delete))
          chpl_internal_error("serial key not created");
        else if (pthread_setspecific(serial_key, p))
          chpl_internal_error("serial state not created");
      }
    }
  }
  else *p = state;
}


//
// This signal handler walks over a linked list with one node per thread.
// If a thread is waiting on a sync or single variable, it sets its
// maybeLocked field first. When the signal is caught, print the locations
// of all threads that have the maybeLocked field set.
//
static void traverseLockedThreads(int sig) {
  lockReport* rep;
  signal(sig, SIG_IGN);
  if (!blockreport)
    return; // Error: this should only be called as a signal handler
            // and it should only be handled if blockreport is on
  rep = lockReportHead;
  while (rep != NULL) {
    if (rep->maybeLocked) {
      if (rep->lineno > 0 && rep->filename)
        fprintf(stderr, "Waiting at: %s:%d\n", rep->filename, rep->lineno);
      else if (rep->lineno == 0 && !strcmp(rep->filename, idleThreadName))
        fprintf(stderr, "Waiting for more work\n");
    }
    rep = rep->next;
  }
  exitChplThreads();
  _chpl_exit_any(1);
}


static void setBlockingLocation(int lineno, _string filename) {
  lockReport* lockRprt;
  if (!blockreport)
    return;
  lockRprt = (lockReport*)pthread_getspecific(lock_report_key);
  lockRprt->filename = filename;
  lockRprt->lineno = lineno;
  lockRprt->maybeLocked = 1;
}


static void unsetBlockingLocation() {
  lockReport* lockRprt;
  if (!blockreport)
    return;
  lockRprt = (lockReport*)pthread_getspecific(lock_report_key);
  lockRprt->maybeLocked = 0;
}


//
// This function should be called exactly once per pthread (not task!),
// including the main thread. It should be called before the first task
// this thread was created to do is started.
//
static void initializeLockReportForThread() {
  lockReport* newLockReport;
  if (!blockreport)
    return;
  newLockReport = chpl_alloc(sizeof(lockReport), "lockReport", 0, 0);
  newLockReport->next = NULL;
  newLockReport->maybeLocked = 0;
  pthread_setspecific(lock_report_key, newLockReport);

  // Begin critical section
  chpl_mutex_lock(&report_lock);
  if (lockReportHead) {
    lockReportTail->next = newLockReport;
    lockReportTail = newLockReport;
  } else {
    lockReportHead = newLockReport;
    lockReportTail = newLockReport;
  }
  // End critical section
  chpl_mutex_unlock(&report_lock);
}


//
// This function removes tasks at the beginning of the task pool
// that have already started executing.
// assumes threading_lock has already been acquired!
//
static void skip_over_begun_tasks (void) {
  while (task_pool_head && task_pool_head->begun) {
    chpl_task_pool_p task = task_pool_head;
    task_pool_head = task_pool_head->next;
    chpl_free(task, 0, 0);
    if (task_pool_head == NULL)  // task pool is now empty
      task_pool_tail = NULL;
  }
}

//
// thread wrapper function runs the user function, waits for more
// tasks, and runs those as they become available
//
static void
chpl_begin_helper (chpl_task_pool_p task) {

  while (true) {
    //
    // reset serial state
    //
    chpl_set_serial(task->serial_state);

    (*task->fun)(task->arg);

    // begin critical section
    chpl_mutex_lock(&threading_lock);

    if (task->task_list)
      task->task_list->completed = true;
    chpl_free(task, 0, 0);  // make sure task_pool_head no longer points to this task!

    //
    // finished task; decrement running count
    //
    running_cnt--;

    //
    // wait for a task to be added to the task pool
    //
    do {
      while (!task_pool_head) {
        setBlockingLocation(0, idleThreadName);
        pthread_cond_wait(&wakeup_signal, &threading_lock);
        if (task_pool_head)
          unsetBlockingLocation();
      }
      // skip over any tasks that have already started executing
      skip_over_begun_tasks();
    } while (!task_pool_head);

    assert (task_pool_head && !task_pool_head->begun);

    if (waking_cnt > 0)
      waking_cnt--;

    //
    // start new task; increment running count and remove task from pool
    //
    running_cnt++;
    task = task_pool_head;
    task->begun = true;
    task_pool_head = task_pool_head->next;
    if (task_pool_head == NULL)  // task pool is now empty
      task_pool_tail = NULL; 
    else if (waking_cnt > 0)
      // schedule another task if one is waiting; this must be done in
      // case, for example, 2 signals were performed by chpl_begin()
      // back-to-back before any thread was woken up from the
      // pthread_cond_wait just above.  In that case, the thread which
      // does eventually wake up is responsible for making sure the other
      // signal is handled (either by an existing thread or by creating
      // a new thread)
      pthread_cond_signal(&wakeup_signal);

    // end critical section
    chpl_mutex_unlock(&threading_lock);
  }
}


//
// run task in a new thread
// assumes at least one task is in the pool and threading_lock has already been acquired!
//
static void
launch_next_task(void) {
  pthread_t        thread;
  chpl_task_pool_p task;
  static _Bool warning_issued = false;

  if (warning_issued)  // If thread creation failed previously, don't try again!
    return;

  // skip over any tasks that have already started executing
  skip_over_begun_tasks();

  if ((task = task_pool_head)) {
    if (pthread_create(&thread, NULL, (chpl_threadfp_t)chpl_begin_helper, task)) {
      char msg[256];
      if (maxThreads)
        sprintf(msg, "maxThreads is %"PRId32", but unable to create more than %d threads",
                maxThreads, threads_cnt);
      else
        sprintf(msg, "maxThreads is unbounded, but unable to create more than %d threads",
                threads_cnt);
      chpl_warning(msg, 0, 0);
      warning_issued = true;
    } else {
      threads_cnt++;
      running_cnt++;
      task->begun = true;
      pthread_detach(thread);
      task_pool_head = task_pool_head->next;
      if (task_pool_head == NULL)  // task pool is now empty
        task_pool_tail = NULL;
    }
  }
}


// Schedule one or more tasks either by signaling an existing thread or by
// launching new threads if available
static void schedule_next_task(int howMany) {
  // if there is an idle thread, send it a signal to wake up and grab
  // a new task
  if (threads_cnt > running_cnt + waking_cnt) {
    // increment waking_cnt by the number of idle threads
    int idle_cnt = threads_cnt - running_cnt - waking_cnt;
    if (idle_cnt >= howMany) {
      waking_cnt += howMany;
      howMany = 0;
    } else {
      waking_cnt += idle_cnt;
      howMany -= idle_cnt;
    }
    pthread_cond_signal(&wakeup_signal);
  }

  //
  // try to launch each remaining task in a new thread
  // if the maximum number threads has not yet been reached
  // take the main thread into account (but not when counting idle
  // threads above)
  //
  for (; howMany && (maxThreads == 0 || threads_cnt + 1 < maxThreads); howMany--)
    launch_next_task();
}


// create a task from the given function pointer and arguments
// and append it to the end of the task pool
// assumes threading_lock has already been acquired!
static chpl_task_pool_p add_to_task_pool (chpl_threadfp_t fp, chpl_threadarg_t a,
                                          _Bool serial, chpl_task_list_p task_list) {
  chpl_task_pool_p task = (chpl_task_pool_p)chpl_alloc(sizeof(task_pool_t), "task pool entry", 0, 0);
  task->fun = fp;
  task->arg = a;
  task->serial_state = serial;
  task->task_list = task_list;
  task->begun = false;
  task->next = NULL;

  if (task_pool_tail)
    task_pool_tail->next = task;
  else
    task_pool_head = task;
  task_pool_tail = task;
  return task;
}


//
// interface function with begin-statement
//
int
chpl_begin (chpl_threadfp_t fp,
            chpl_threadarg_t a,
            chpl_bool ignore_serial,  // always add task to pool
            chpl_bool serial_state) {
  if (!ignore_serial && chpl_get_serial()) {
    (*fp)(a);
  } else {
    // begin critical section
    chpl_mutex_lock(&threading_lock);

    add_to_task_pool (fp, a, serial_state, NULL);

    schedule_next_task(1);

    // end critical section
    chpl_mutex_unlock(&threading_lock);
  }
  return 0;
}

void chpl_add_to_task_list (chpl_threadfp_t fun, chpl_threadarg_t arg, chpl_task_list_p *task_list) {
  chpl_task_list_p task = (chpl_task_list_p)chpl_alloc(sizeof(struct chpl_task_list), "task list entry", 0, 0);
  task->fun = fun;
  task->arg = arg;
  task->completed = false;
  if (*task_list) {
    task->next = (*task_list)->next;
    (*task_list)->next = task;
  }
  else task->next = task;
  *task_list = task;
}

void chpl_process_task_list (chpl_task_list_p task_list) {
  // task_list points to the last entry on the list; task_list->next is actually
  // the first element on the list.
  chpl_task_list_p task = task_list, next_task;
  _Bool serial = chpl_get_serial();
  // This function is not expected to be called if a cobegin contains fewer
  // than two statements.
  assert (task && task->next != task);
  next_task = task->next;  // next_task now points to the head of the list

  if (serial) {
    do {
      task = next_task;
      (*task->fun)(task->arg);
      next_task = task->next;
      chpl_free (task, 0, 0);
    } while (task != task_list);
  }

  else {
    int task_cnt = 0;
    chpl_task_list_p first_task = next_task;
    next_task = next_task->next;

    // begin critical section
    chpl_mutex_lock(&threading_lock);

    do {
      task = next_task;
      task->task_pool_entry = add_to_task_pool (task->fun, task->arg, serial, task);
      next_task = task->next;
      task_cnt++;
    } while (task != task_list);

    schedule_next_task(task_cnt);

    // end critical section
    chpl_mutex_unlock(&threading_lock);

    // Execute the first task on the list, since it has to run to completion
    // before continuing beyond the cobegin it's in.
    (*first_task->fun)(first_task->arg);
    next_task = first_task->next;
    chpl_free (first_task, 0, 0);

    do {

      task = next_task;
      next_task = task->next;

      // don't lock unnecessarily
      if (!task->completed) {
        chpl_threadfp_t  task_to_run_fun = NULL;
        chpl_threadarg_t task_to_run_arg = NULL;

        // begin critical section
        chpl_mutex_lock(&threading_lock);

        if (!task->completed) {
          if (task->task_pool_entry->begun)
            // task is about to be freed; the completed field should not be accessed!
            task->task_pool_entry->task_list = NULL;
          else {
            task_to_run_fun = task->task_pool_entry->fun;
            task_to_run_arg = task->task_pool_entry->arg;
            task->task_pool_entry->begun = true;
            if (waking_cnt > 0)
              waking_cnt--;
          }
        }

        // end critical section
        chpl_mutex_unlock(&threading_lock);

        if (task_to_run_fun)
          (*task_to_run_fun) (task_to_run_arg);
      }

      chpl_free (task, 0, 0);

    } while (task != task_list);
  }
}
