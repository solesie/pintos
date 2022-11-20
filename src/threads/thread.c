#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

#include "threads/fixed-point.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */
/* Project #3 */
bool thread_prior_aging;
/* sema_up에서 thread_yield()는 vtop()을 호출하는데, 이를 사용하려면 paging_init()을 먼저 호출해야 한다.
   thread가 시작되고 나서 yield 되는 것이 옳다고 여긴다. */
bool threading_started = false;

static int load_avg;            /* 1분동안 수행가능한 스레드의 평균 개수, 크면 priority는 천천히 증가 */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  load_avg = 0;
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  
  list_init(&sleep_queue);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->nice = 0;
  initial_thread->recent_cpu = 0;
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  threading_started = true;
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  /* thread_unblock()을 통해 새로 만든 스레드를 READY list에 넣었다.
     이때, 이 새로만든 스레드의 우선순위가 현재 실행되고 있는 스레드의 우선순위보다 높다면, CPU를 양보한다.
     물론 interrupt가 걸려서 양보할 가능성이 있는 상태이다. 이를 강제한다. */
  if(priority > thread_get_priority()){
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  if(!threading_started) return;
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, thread_priority_comparator, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim.
   intr_handler() 는 외부인터럽트 중에서도 타이머 인터럽트인 경우에만, intr_off된 상태로 이걸 호출한다. */
void
thread_yield (void) 
{
  if(!threading_started) return;
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, thread_priority_comparator, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if(thread_mlfqs) return;
  int old_priority = thread_current()->priority;
  thread_current ()->priority = new_priority;
  /* 현재 스레드의 새로운 priority가 더 작아지게 된다면 더 높은 priority를 가진 스레드가 실행되게 한다.
     이 때 현재 스레드가 그대로 수행될 수도 있다. */
  if(new_priority < old_priority){
    thread_yield();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  struct thread* t = thread_current();
  t->nice = nice;
  update_priority(t);

  /* 지금 돌아가는 스레드가 우선순위가 더 낮아졌을 수도 있다. */
  thread_yield();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current()->nice; 
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return fp_mul_int(load_avg, 100) / FRACTION_SHIFT;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return fp_mul_int(thread_current()->recent_cpu, 100) / FRACTION_SHIFT;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
#ifdef USERPROG
  //fd 초기화
  memset(t->fd, NULL, sizeof(t->fd));
  //child semaphore 초기화
  sema_init(&(t->exit_sema), 0);
  sema_init(&(t->wait_sema), 0);
  list_init(&(t->child));
  list_push_back(&(running_thread()->child), &(t->child_elem));
#endif

  /* pintos manual: recent_cpu, nice는 부모 thread로 부터 상속된 값을 가진다. */
  t->recent_cpu = running_thread()->recent_cpu; 
  t->nice = running_thread()->nice; 
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   이 함수의 호출 시, 우리는 방금 스레드 PREV에서 전환했고, 새 스레드는 이미 실행 중이고,
   인터럽트는 여전히 비활성화되어 있습니다. This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

void update_next_tick_to_awake(int64_t ticks){
  next_tick_to_awake = (next_tick_to_awake > ticks) ? ticks : next_tick_to_awake;
}
int64_t get_next_tick_to_awake(void){
  return next_tick_to_awake;
}
void thread_sleep(int64_t ticks){
  /* 해당 과정중에는 인터럽트를 받아들이지 않는다. blocked 상태가 되고있는 도중에 ready가 되면 문제가 생길 것이다. */
  enum intr_level old_level;
  old_level = intr_disable();
  struct thread *cur = thread_current();
  /* 현재 스레드가 idle 스레드가 아닐경우 thread의 상태를 BLOCKED로 바꾸고 next_tick_to_awake를 업데이트한다.*/
  ASSERT(cur != idle_thread);
  update_next_tick_to_awake(cur ->wakeup_tick = ticks);
  list_push_back(&sleep_queue, &cur->elem);
  /* 이 스레드를 블락하고 다시 READY list에 있는 thread를 실행 */
  thread_block();
  /* 다시 interrupt를 받아들이도록 한다. */
  intr_set_level(old_level);
}
void thread_awake(int64_t ticks){
  next_tick_to_awake = INT64_MAX;
  struct list_elem *e = list_begin(&sleep_queue);
  while(e != list_end(&sleep_queue)){
    struct thread * t = list_entry(e, struct thread, elem);
    /* 현재 tick이 깨워야 할 tick 보다 크거나 같다면 슬립 큐에서 제거하고 unblock 한다. */
    if(ticks >= t->wakeup_tick){
      e = list_remove(&t->elem);
      thread_unblock(t);
    }
    /* 아직 깨우면 안되는 thread들 중에서 next_tick_to_awake를 갱신한다. */
    else{
      e = list_next(e);
      update_next_tick_to_awake(t->wakeup_tick);
    }
  }
}

bool thread_priority_comparator(const struct list_elem* left, const struct list_elem* right, void* aux){
  struct thread *thread_left = list_entry(left, struct thread, elem);
  struct thread *thread_right = list_entry(right, struct thread, elem);
  return thread_left->priority > thread_right->priority;
}

void update_priority(struct thread *t){
	/* idle_thread(main thread)의 priority는 고정이다. */
    if (t != idle_thread) {
        int recent_cpu_div4 = fp_div_int(t->recent_cpu, 4);
        int nice_mul_2 = 2 * t->nice;
        int64_t temp = fp_add_int(recent_cpu_div4, nice_mul_2);
        int pri_result = fp_sub_fp(fp_add_int(0,PRI_MAX), temp) / FRACTION_SHIFT;
        
        if (pri_result < PRI_MIN)
            pri_result = PRI_MIN;
        if (pri_result > PRI_MAX)
            pri_result = PRI_MAX;
        t->priority = pri_result;
    }
}
void update_recent_cpu(struct thread *t){
	/* idle_thread(main thread)의 recent_cpu는 고정이다. */
    if (t != idle_thread) {
        int load_avg_mul2 = fp_mul_int(load_avg, 2);
        int load_avg_mul2_add1 = fp_add_int(load_avg_mul2, 1);
        int result = fp_mul_fp(fp_div_fp(load_avg_mul2, load_avg_mul2_add1), t->recent_cpu);
        result = fp_add_int(result, t->nice);
        /* recent_cpu는 음수가 될 수 없다. */
        if ((result >> 31) == (-1) >> 31) {
            result = 0;
        }
        t->recent_cpu = result;
    }
}
void update_load_avg(void){
	// load_avg = (59/60) * load_avg + (1/60) * ready_threads;
    int ready_threads = (int)list_size(&ready_list);
    /* 현재 스레드도 고려해서 +1을 해준다. */
    ready_threads = (thread_current() == idle_thread) ? ready_threads : ready_threads + 1;
    load_avg = fp_div_int(fp_add_int(fp_mul_int(load_avg, 59), ready_threads), 60);
}

void increment_running_thread_recent_cpu(void){
  if (thread_current() != idle_thread) {
        int cur_recent_cpu = thread_current()->recent_cpu;
        thread_current()->recent_cpu = fp_add_int(cur_recent_cpu, 1);
  }
}

void update_all_thread_recent_cpu(void) {
    for (struct list_elem *tmp = list_begin(&all_list); tmp != list_end(&all_list); tmp = list_next(tmp)) {
        update_recent_cpu(list_entry(tmp, struct thread, allelem));
    }
}

void update_all_thread_priority(void) {
    for (struct list_elem *tmp = list_begin(&all_list); tmp != list_end(&all_list); tmp = list_next(tmp)) {
        update_priority(list_entry(tmp, struct thread, allelem));
    }
}


/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
