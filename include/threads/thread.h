#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
/*#####Newly added in Project 2#####*/
#include "threads/synch.h"
/*#################################*/
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/*#####Newly added in Project 2#####*/
/*#######File System Call#######*/
#define FDT_PAGES 3  /* 8bytes */
#define FDCOUNT_LIMIT FDT_PAGES *1 << 9 //Limit fdIdx

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/*################Newly added in Project 1#################*/
	/*-----------------------Alarm Clock----------------------*/
	int64_t wakeup_tick; 				/*Tick till wake up. */
	/*-------------------Priority Donation--------------------*/
	int original_priority;  			/* Original priority */
	struct lock *lock_for_wait; 			/* Address of which the thread is wating for lock */
	struct list donors;					/* Priority donors list */
	struct list_elem donor_elem; 		/* Donors list element */
	/*#########################################################*/

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	/*###############Newly added in Project 2###############*/
	/*-----------------Process System Call------------------*/
	struct list children; 	/* Child process list */
	struct list_elem child_elem; /* Child process list element */
	struct semaphore wait_sema; /* Semaphore for wait() - to signal to parent process*/
	struct semaphore fork_sema; /* Semaphore for fork() */
	struct semaphore reap_sema; /* Semaphore for reaping zombie child processes */

	struct intr_frame if_; /* Interrupt frame for switching */

	int exit_status;   /* for exit(), wait()*/
	// int wait_call_count; 
	/*-----------------File System Call --------------------*/
	struct file **fdt; /* Pointer to File Descriptor Table*/
	int next_fd; /* Index for open spot in FDT */
	struct file *running; /* Running File */
	/*#####################################################*/

	

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

/******Newly added function in project 1*******/
void thread_sleep (int64_t ticks);				/*Turns thread state from THREAD_RUNNING into THREAD_BLOCKED*/
void thread_awake (int64_t ticks);				/*Awakes a thread in sleep_list*/
void update_next_tick_to_awake (int64_t ticks); 	/*Saves a thread that has a minimum tick*/
int64_t get_next_tick_to_awake (void); 			/*Returns next_tick_to_awake(global variable in thread.c)*/ 
/*Project 1 (Priority Scheduling)*/
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux);
void test_max_priority (void);
/*############################################*/
/*############Newly added in Project 2###############*/
/*---------------Process System Call-----------------*/
struct thread *find_child_process (tid_t child_tid);
/*###################################################*/
#endif /* threads/thread.h */
