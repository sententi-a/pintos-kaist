/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) { // 그냥 안정성을 위해 if 대신 while문을 사용한다.
		// list_push_back (&sema->waiters, &thread_current ()->elem); : 맨 뒤에 넣는 함수이기에 주석
		// semaphore --------------------------------------------------------------------

		// 세마포어 큐을 우선순위로 정렬시켜서 넣기위해
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);

		// semaphore --------------------------------------------------------------------
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	// semaphore --------------------------------------------------------------------

	if (!list_empty(&sema->waiters)){
		// 대기 리스트에 있던 동안 우선순위가 변했을수도 있으니 다시 정렬?
		list_sort(&sema->waiters, &cmp_priority, NULL);	//w정렬 시켜주는 함수?
		thread_unblock(list_entry(list_pop_front(&sema->waiters), struct thread, elem));
	}
	sema->value ++;
	// 언블락된 스레드가 현재 스레드보다 우선순위가 높을수 있으니 test_max_priority 호출
	test_max_priority();

	// semaphore --------------------------------------------------------------------
	
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	// Priority Donation -------------------------------------------------------------

	struct thread *t = thread_current();
	if (lock->holder != NULL){ // 락의 holder(소유자)가 존재하여 대기해야할 경우

		t->wait_on_lock = lock; // 대기할 락의 자료구조 저장
		list_push_back(&lock->holder->donations, &t->donation_elem); // 대기자 리스트에 추가
		donation_priority(); // priority donation을 수행할 함수 호출
	}
	sema_down (&lock->semaphore);
	// lock->holder = thread_current (); : 원래 코드
	// 락을 획득 후 락의 holder 갱신
	t->wait_on_lock = NULL;
	lock->holder = t;

	// Priority Donation -------------------------------------------------------------
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	// Priority Donation -------------------------------------------------------------
	remove_with_lock(lock); // 도네이션 리스트에서 해지할 락을 가진 엔트리 제거
	refresh_priority(); // 위에서 엔트리를 제거하므로 변경해야 할 우선 순위를 다시 계산
	// Priority Donation -------------------------------------------------------------

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// semaphore --------------------------------------------------------------------

	// list_push_back (&cond->waiters, &waiter.elem); 원래는 리스트 맨뒤에 추가하는 코드이다.
	list_insert_ordered(&cond->waiters, &waiter.elem, &cmp_sem_priority, NULL);

	// semaphore --------------------------------------------------------------------
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
	// semaphore --------------------------------------------------------------------
		list_sort(&cond->waiters, &cmp_sem_priority, NULL); // 대기 리스트를 우선순위로 정렬
	// semaphore --------------------------------------------------------------------
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

// semaphore --------------------------------------------------------------------

bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux){
	
	// 각각의 인자가 속한 semaphore_elem을 얻어온다.
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	// 얻어온 semaphore_elem을 통해 thread의 list_elem을 얻어온다.
	struct list_elem *sa_e = list_begin(&(sa->semaphore.waiters));
	struct list_elem *sb_e = list_begin(&(sb->semaphore.waiters));

	// 얻어온 thread의 list_elem으로 thread구조체를 얻어온다.
	struct thread *t_a = list_entry(sa_e, struct thread, elem);
	struct thread *t_b = list_entry(sb_e, struct thread, elem);

	if ((t_a->priority) > (t_b->priority)){
		return true;
	}else{
		return false;
	}
}
// semaphore --------------------------------------------------------------------

// Priority Donation -------------------------------------------------------------

void donation_priority(void){

	struct thread *t = thread_current(); // 현재 실행중인 스레드를 가져온다.
	int curr_p = t->priority; // 현재 실행중인 스레드의 우선순위를 변수에 담는다.
	int count = 0; // nested의 최대 깊이가 8이므로 그걸 넘는걸 방지할 변수

	while (count < 9){ // 카운트가 8보다 커지지 않게 while문을 돌림

		count ++;
		if (t->wait_on_lock == NULL){ //만약 해당 스레드가 필요한 락이 없을 경우 종료
			break;
		}
		t = t->wait_on_lock->holder; // 스레드 변수 t에 t가 필요로 하는 락을 가진 소유자의 스레드를 대입
		t->priority = curr_p; // t 스레드에 처음 실행중인 스레드의 우선순위를 넣어준다.
	}
}
// Priority Donation -------------------------------------------------------------

// Priority Donation -------------------------------------------------------------

void remove_with_lock(struct lock *lock){
	
	struct thread *t = thread_current(); // 현재 실행중인 스레드를 가져온다.
	struct list_elem *e = list_begin(&(t->donations)); // 현재 진행중인 스레드의 도네이션 리스트의 맨앞값을 가져온다.

	for (e; e != list_end(&t->donations);){ // for문으로 도네이션 리스트를 돌아본다.
		
		//도네이션 리스트의 elem값으로 해당 스레드를 가져온다.
		struct thread *curr = list_entry(e, struct thread, donation_elem); 
		if (curr->wait_on_lock == lock){
			// 같은 락을 가진다면 지워준다.
			e = list_remove(e);
		}else{
			// 같지 않다면 e의 값을 e다음 값으로 변경
			e = list_next(e);
		}
	}
}
// Priority Donation -------------------------------------------------------------

// Priority Donation -------------------------------------------------------------
// 락이 해제되었 때, 우선순위를 갱신하는 함수
void refresh_priority(void){

	struct thread *curr = thread_current(); //현재 실행중인 스레드를 가져온다.
	curr->priority = curr->init_priority; // 현재 실행중인 스레드를 원래 우선순위로 변경

	if (list_empty(&curr->donations) == false){ // 현재 실행중인 스레드의 도네이션 리스트가 비어있지 않을경우
		
		struct thread *h;
		list_sort(&curr->donations, &cmp_priority, NULL); // 도네이션 리스트를 우선순위로 정렬
		// 정렬된 도네이션 리스트의 가장 앞에 있는 값을 h구조체에 담는다.
		h = list_entry(list_front(&curr->donations), struct thread, donation_elem);

		// 만일 도네이션 리스트의 맨앞의 값이 현재 스레드의 우선순위보다 크다면 현재 스레드의 우선순위를 바꾸어준다.
		if (h->priority > curr->priority){
			curr->priority = h->priority;
		}
	}
}
// Priority Donation -------------------------------------------------------------