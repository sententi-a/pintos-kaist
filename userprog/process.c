#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
// System Call ----------------------------------------------------------------------
// #include "threads/synch.h"
#include "lib/user/syscall.h"
// System Call ----------------------------------------------------------------------
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

void argument_stack(char** token_list, int count, struct intr_frame *if_);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	// System Call ----------------------------------------------------------------------
	char *tok, *save_pt;
	tok = strtok_r(file_name, " ", &save_pt);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (tok, PRI_DEFAULT, initd, fn_copy);
	// System Call ----------------------------------------------------------------------
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/

	// System Call ----------------------------------------------------------------------
	struct thread *parent = thread_current();
	memcpy(&parent->parent_if, if_, sizeof(struct intr_frame));

	tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, parent);

	if (tid == TID_ERROR){
		return TID_ERROR;
	}

	struct thread *child = get_child_pid(tid);
	sema_down(&child->fork_sema);

	if (child->exit_status == -1) {
		return -1;
	}
	return tid;
	// System Call ----------------------------------------------------------------------
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)){
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL){
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);
	if (newpage == NULL){
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	// System Call ----------------------------------------------------------------------
	struct intr_frame *parent_if;
	parent_if = &parent->parent_if;
	// System Call ----------------------------------------------------------------------
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	// System Call ----------------------------------------------------------------------
	if (parent->fdidx == FDT_COUNT_LIMIT){
		goto error;
	}

	for (int i = 0; i < FDT_COUNT_LIMIT; i++){
		struct file *f = parent->file_dt[i];

		if (f == NULL){
			continue;
		}
		struct file *cp_file;

		if (f > 2){
			cp_file = file_duplicate(f);
		}
		else{
			cp_file = f;
		}
		current->file_dt[i] = cp_file;
	}
	// fork()를 통해 만들어진 프로세스의 실행 파일참조 멤버를 부모의 실행파일을 복제한 것과 매핑한다.
	// current->running = file_duplicate(parent ->running);
	// file_allow_write(current->running);

	current->fdidx = parent->fdidx;
	sema_up(&current->fork_sema);
	if_.R.rax = 0;

	// System Call ----------------------------------------------------------------------

	//수정사항입니다 주석처리함
	//process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	//수정사항입니다
	current->exit_status = TID_ERROR;
	sema_up(&current->fork_sema);
	exit(-1);
	//thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int process_exec (void *f_name) {
	// f_name은 문자열인데 위에서 (void *)로 넘겨받았다. -> 문자열로 인식하기 위해서 char * 로 변환해줘야한다.
	char *file_name = f_name;
	bool success;

	// Commend Line Parsing -------------------------------------------------------------
	// char *copy_name;
	// memcpy(copy_name, file_name, strlen(file_name) + 1); // +1 해주는 이유는 문자열의 마지막에 '\n'값도 포함해주어야 해서
	// Commend Line Parsing -------------------------------------------------------------

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	// intr_frame 내 구조체 멤버에 필요한 정보를 담는다.
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	// 새로운 실행 파일을 현재 스레드에 담기전에 현재 프로세스에 할당된 page directory를 지워준다.
	process_cleanup ();

	// Commend Line Parsing -------------------------------------------------------------
	// 입력 받은 문자열을 공백 기준으로 파싱해준다.

	int count = 0; // argument의 개수
	char *token, *save_pt; // token : 분리된 문자열, save_pt : 분리된 문자열 중 남는 부분의 시작주소
	char *token_list[128]; // argument 배열
	
	token = strtok_r(file_name, " ", &save_pt);
	char *tmp_save = token; // 문자열의 첫 부분(파일명)을 따로 변수에 담아 저장
	token_list[count] = token; // 해당 파싱 순서대로 배열에 저장

	while (token != NULL){
		token = strtok_r(NULL, " ", &save_pt);
		count++;
		token_list[count] = token;
	}
	/* And then load the binary */
	// load를 실패했다면 새로운 실행 파일의 정보를 프로세스(레지스터)에 로드 시킨다?
	success = load(tmp_save, &_if);

	// Commend Line Parsing -------------------------------------------------------------

	/* If load failed, quit. */
	// 현재 돌아가는 프로세스에 할당된 메모리 반환

	if (!success){
		return -1;
	}
	argument_stack(token_list, count, &_if); // 스택에 인자를 넣어주는 함수

	palloc_free_page (file_name);
	//hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true); // 프로그램의 스택 메모리 출력 함수

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}

// Commend Line Parsing -------------------------------------------------------------
// 파싱된 인자를 스택에 저장하는 함수
void argument_stack(char** token_list, int count, struct intr_frame *if_){

	char *arg_address[128]; // 각각 인자들의 스택 주소를 저장하는 배열

	// 파싱된 인자들 스택에 넣는 작업
	for (int i = count-1; i >= 0 ; i--){ // 현재 count가 실제 인자의 수보다 1많기에 -1해준다.

		int token_len = strlen(token_list[i]) + 1;
		if_->rsp -= token_len; // 스택 포인터를 토큰길이만큰 내려준다.(인자를 넣을 공간을 만드는과정)
		// if_->rsp : 현재 스택위치의 포인터
		memcpy(if_->rsp, token_list[i], token_len); // 확보한 공간에 인자를 복사
		arg_address[i] = if_->rsp; // 인자들의 스택 시작 주소값을 저장
	}
	// 8의 배수를 맞추기 위한 작업
	while (if_->rsp % 8 != 0){ // 8의 배수가 아닐때까지
		if_->rsp--; // 1씩 공간을 만든다.
		// 스택은 반대 방향으로 확장하기에 -하는게 공간을 만드는 작업이다.
		*(uint8_t*)(if_->rsp) = 0; // 만든 공간에 0값 대입
	}
	// 각각 인자들의 스택 주소를 넣어주는 작업
	for (int i = count; i >= 0; i--){
		if_->rsp -= 8; // 스택에 자료를 넣어줄 공간 확보
		if (i == count){ // 가장 처음에는 0을 넣어준다.
			memset(if_->rsp, 0, sizeof(char**));
		}else{ // 이후부터는 각 인자들의 스택 주소를 넣어준다.
			memcpy(if_->rsp, &arg_address[i], sizeof(char**));
		}
	}
	// return address 주소 생성
	if_->rsp -= 8; // 공간 확보
	memset(if_->rsp, 0, sizeof(void*)); // 0을 넣어준다.

	if_->R.rdi = count; // 인자의 개수를 저장
	if_->R.rsi = if_->rsp + 8; // return address의 시작 주소값을 저장
}

// Commend Line Parsing -------------------------------------------------------------

/*스레드 TID가 종료될 때까지 기다렸다가 종료 상태를 반환합니다. 
커널에 의해 종료된 경우(예외로 인해 종료된 경우) -1을 반환합니다.
TID가 잘못되었거나 호출 프로세스의 하위 항목이 아니거나 
지정된 TID에 대해 process_wait()가 이미 성공적으로 호출된 경우 
기다리지 않고 -1을 즉시 반환합니다.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	// Commend Line Parsing ----------------------------------------------------
	// 임시
	// for(int i=0;i<1000000000;i++);
	// Commend Line Parsing ----------------------------------------------------

	// System Call ----------------------------------------------------------------------
	struct thread *child = get_child_pid(child_tid);

	if (child == NULL){
		return -1;
	}

	sema_down(&child->wait_sema);
	int exit_status = child->exit_status;
	list_remove(&child->child_elem);
	sema_up(&child->free_sema);

	return exit_status;

	// System Call ----------------------------------------------------------------------
	// return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	// System Call ----------------------------------------------------------------------
	// 열려 있는 모든 파일 닫아주기
	for (int i = 0; i < FDT_COUNT_LIMIT; i++){
		close(i);
	}

	// 할당 받은 FDT 해제
	palloc_free_multiple(curr->file_dt, FDT_PAGES);
	file_close(curr->running);

	
	// semaphore 처리?
	sema_up(&curr->wait_sema);
	sema_down(&curr->free_sema);

	process_cleanup ();
	// System Call ----------------------------------------------------------------------
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	// 페이지 디렉토리 생성
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ()); // 페이지 테이블 활성화

	/* Open executable file. */
	// 프로그램 파일 오픈
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	// System Call ----------------------------------------------------------------------
	file_deny_write(file); // 파일 쓰기 금지 함수
	t->running = file;
	// System Call ----------------------------------------------------------------------

	/* Read and verify executable header. */
	// ELF 파일의 헤더 정보를 읽어와 저장
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	// 배치 정보를 읽어와 저장
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					// 배치 정보를 통해 파일을 메모리에 적재
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	// 스택 초기화
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	//수정사항입니다 주석처리했음
	//file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */