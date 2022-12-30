#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
// System Call ----------------------------------------------------------------------
#include "threads/synch.h"
// System Call ----------------------------------------------------------------------
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

// System Call ----------------------------------------------------------------------
#define FDT_PAGES 3
#define FDT_COUNT_LIMIT FDT_PAGES *(1<<9)
// System Call ----------------------------------------------------------------------


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
	tid_t tid;                          /* 스레드의 식별자 */
	enum thread_status status;          /* 스레드 상태(3가지 상태가 있다) */
	char name[16];                      /* 스레드의 이름이나 축약어를 기록 */
	int priority;                       /* 스레드 우선순위 */
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* 스레드를 이중연결 리스트에 넣기 위해 쓰인다. */

	// Alarm Clock -------------------------------------------------------------------
	int64_t wakeup_tick;
	// Alarm Clock -------------------------------------------------------------------
	
	// Priority Donation -------------------------------------------------------------
	int init_priority; // 초기 우선순위 값을 저장할 필드
	struct lock *wait_on_lock; // lock 자료구조의 주소를 저장할 필드
	struct list donations; // multiple donation을 위한 리스트
	struct list_elem donation_elem; // 위의 리스트를 위한 elem
	// Priority Donation -------------------------------------------------------------

	// System Call ----------------------------------------------------------------------
	int exit_status; // 자식이 살았는지 죽었는지 판단하기 위한 변수
	struct file **file_dt; // 파일 디스크립트 테이블을 넣어줄 필드
	int fdidx; // FDT의 인덱스 : 파일을 찾기위한
	struct list child_list; // 자식 프로세스를 모아두는 필드
	struct list_elem child_elem; // 자식 프로세스를 찾기 위한 elem
	struct semaphore wait_sema; // 부모가 자식 프로세스가 죽을때 까지 대기
	struct semaphore fork_sema; // fork에서 자식 프로세스의 복제가 완전히 끝날때까지 대기
	struct semaphore free_sema; // 자식의 exit_status를 받을때 까지 대기
	struct intr_frame parent_if; // fork 호출시 자식 프로세스에게 복사해줄 인터럽트 프레임
	struct file *running;
	// System Call ----------------------------------------------------------------------


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
	unsigned magic;                     /* 스택 오버플로우를 탐지하기 위해 쓰인다. */
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

// alarm clock -------------------------------------------------------------------
// 함수 선언
void thread_sleep(int64_t ticks); // 실행 중인 스레드를 슬립으로 재운다.
void thread_awake(int64_t ticks); // 슬립 큐에서 꺠워야 할 스레드를 깨운다.
void update_next_tick_to_awake(int64_t ticks); // 최소 틱을 가진 스레드를 저장한다.
int64_t get_next_tick_to_awake(void); // next_tick_to_awake 반환
// alarm clock -------------------------------------------------------------------

// Priority Scheduling -----------------------------------------------------------
// 함수 선언
void test_max_priority(void);
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
// Priority Scheduling -----------------------------------------------------------

// Priority Donation -------------------------------------------------------------
//함수 선언
void donation_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);
// Priority Donation -------------------------------------------------------------
struct thread *get_child_pid(int pid);
#endif /* threads/thread.h */
