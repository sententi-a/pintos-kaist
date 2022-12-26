#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

/*#####Newly added in Project 2#####*/
/*#####File Manipulation#####*/
#include "filesys/file.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/*#####Newly added in Project 2#####*/
/*##### System call #####*/
/*Check validity of address*/
void check_address (void *addr) {
	if (is_user_vaddr(addr)) 
		return true;
	else
		exit(-1);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	/*##### Newly added in Project 2 #####*/
	/*##### System Call #####*/
	// TODO: Your implementation goes here.

	/*1. Check whether stack pointer is in user space*/
	check_address (f->rsp);

	/*2. Copy system call num in stack*/
	int num = *(int *)f->R.rax;
	char *rsp = f->rsp;

	switch(num) {
		/* Process related system calls */
		case SYS_HALT :
			halt();
			break;

		case SYS_EXIT :
			exit();
			break;

		case SYS_FORK :
			break;

		case SYS_EXEC :
			break;

		case SYS_WAIT :
			process_wait();
			break; 
		
		/*File System related system calls*/
		case SYS_CREATE :
			create();
			break;
	
		case SYS_REMOVE : 
			remove (f->R.rdi);
			break;

		case SYS_OPEN :
			open (f->R.rdi);
			// file_open();
			break;

		case SYS_FILESIZE :
			filesize(f->R.rdi);
			// file_length();
			break;
		case SYS_READ :
			read(f->R.rdi, f->R.rsi, f->R.rdx);
			// file_read();
			break;
		case SYS_WRITE : 
			write (f->R.rdi, f->R.rsi, f->R.rdx);
			// file_write();
			break;
		case SYS_SEEK : 
			// file_seek();
			break;
		case SYS_TELL :
			// file_tell();
		 	break;
		case SYS_CLOSE:
			// file_close();
			break;
	}
	/*3. Copy system call arguments and call system call*/
	printf ("system call!\n");
	thread_exit ();
}
