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
/*#####File System call#####*/
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/*#############Newly added in Project 2###############*/
/*-----------------Process System Call------------------*/
typedef int pid_t;
typedef int32_t off_t;

void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
tid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t pid);
/*--------------------File System call------------------*/
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

void check_address (void *addr);

struct file *find_file_by_fd (int fd);
void remove_file_from_fdt (int fd);
int add_file_to_fdt (struct file *file);

/*###################################################*/

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

	/*##############Newly added in Project 2################*/
	/*######################System call######################*/
	lock_init(&filesys_lock);
	/*#####################################################*/
}

/*#####Newly added in Project 2#####*/
/*##### System call #####*/
/* Check validity of address
   Invalid Cases 
   1. addr is a null pointer
   2. *addr is not in user area
   3. page is not allocated */
void check_address (void *addr) {
	struct thread *curr = thread_current ();
	if (addr == NULL || ! is_user_vaddr (addr) || pml4_get_page (curr->pml4, addr) == NULL){
		exit(-1);
	}
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	/*##### Newly added in Project 2 #####*/
	/*##### System Call #####*/
	// TODO: Your implementation goes here.

	/* Copy system call arguments and call system call*/
	//printf ("system call!\n");
	
	switch(f->R.rax) {
		/* Process related system calls */
		case SYS_HALT :
			halt ();
			break;

		case SYS_EXIT :
			exit (f->R.rdi);
			break;

		case SYS_FORK :
			memcpy (&thread_current ()->if_, f, sizeof(struct intr_frame));
			f->R.rax = fork (f->R.rdi);
			break;

		case SYS_EXEC :
			if (exec (f->R.rdi) == -1) {
				exit (-1);
			}
			break;

		case SYS_WAIT :
			f->R.rax = wait (f->R.rdi);
			break; 
		
		/*File System related system calls*/
		case SYS_CREATE :
			f->R.rax = create (f->R.rdi, f->R.rsi);
			break;
	
		case SYS_REMOVE : 
			f->R.rax = remove (f->R.rdi);
			break;

		case SYS_OPEN :
			f->R.rax = open (f->R.rdi);
			break;

		case SYS_FILESIZE :
			f->R.rax = filesize (f->R.rdi);
			break;

		case SYS_READ :
			f->R.rax = read (f->R.rdi, f->R.rsi, f->R.rdx);
			break;

		case SYS_WRITE : 
			f->R.rax = write (f->R.rdi, f->R.rsi, f->R.rdx);
			break;

		case SYS_SEEK : 
			seek (f->R.rdi, f->R.rsi);
			break;

		case SYS_TELL :
			f->R.rax = tell (f->R.rdi);
		 	break;

		case SYS_CLOSE:
			close (f->R.rdi);
			break;

		default:
		 	exit (-1);
		 	break;
	}
	// thread_exit ();
}
/*##################Process System call######################*/
void halt (void) {
	power_off ();
}

/**/
void exit (int status) {
	struct thread *curr = thread_current ();
	curr->exit_status = status;

	printf("%s: exit(%d)\n", curr->name, curr->exit_status);
	thread_exit ();
}

/* Waits for thread TID / process PID to die and returns its exit status */
int wait (pid_t pid) {
	return process_wait (pid);
}

/* Run program which execute 'file' (switch current process)
   Creates thread and run exec()
   Returns pid of the new child process if child load file successfully, -1 on failure 
   Parent calling exec should wait 
   until child process is created and loads the executable completely */
int exec (const char *file) {
	check_address (file);
	int fn_size = strlen(file) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);

	/* page allocation failure */
	if (fn_copy == NULL) {
		exit (-1);
	}

	strlcpy (fn_copy, file, fn_size);

	if (process_exec (fn_copy) == -1) {
		return -1;
	}

	// NOT_REACHED (); /* 이건 왜 넣는 건지....? */
	return 0; 
}


tid_t fork (const char *thread_name) {
	return process_fork (thread_name, &thread_current ()->if_);
}

/*##########################################################*/

/*##################File System call###################*/
/* Create initial_size of file 
   Return true on success, false on failure (already exist / internal memory allocation fail)*/
bool create (const char *file, unsigned initial_size) {
	check_address (file);

	lock_acquire (&filesys_lock);
	bool success = filesys_create(file, initial_size);
	lock_release (&filesys_lock);

	return success;
}

/* Delete a file named 'file'
   Return true on success, false on failure (not exist / internal memory allocation fail)
   A file may be removed regardless of whether it is open or closed
   Removing an open file does not close it*/
bool remove (const char *file) {
	check_address (file);

	lock_acquire (&filesys_lock);
	bool success = filesys_remove (file);
	lock_release (&filesys_lock);

	return success;
}

/* Open file 'file'
   Return nonnegative file descriptor or -1 if the file could not be opened 
   File descriptors numbered 0, 1 are reserved for the console (0:STDIN_FILENO/1:STDOUT_FILENO)
   If file descriptor table is full, close the file
   */
int open (const char *file) {
	check_address (file);

	lock_acquire (&filesys_lock);
	/* Try to open file */
	struct file *open_file = filesys_open (file);
	lock_release (&filesys_lock);

	/* Return -1 when file could not be opened */
	if (open_file == NULL) {
		return -1;
	}

	/* Add file to File descriptor table */
	int fd = add_file_to_fdt (open_file);

	/* Close the file when fdt is full */
	if (fd == -1) {
		file_close (open_file);
	}

	return fd;
}

/* Returns the size, in bytes, of the file open as fd */
int filesize (int fd) {
	struct file *file = find_file_by_fd (fd);

	if (file == NULL) {
		return -1;
	}

	return file_length (file);
}

/* Read size bytes from the file open as fd into buffer
   Return the number of bytes actually read 
   (0 at end of file / -1 if file could not be read)
   If fd is 0, it reads from the keyboard using input_getc() 
*/
int read (int fd, void *buffer, unsigned length) {
	check_address (buffer);
	//check_address (buffer + length - 1);

	int bytes_read;

	/* Standard Input */
	if (fd == STDIN_FILENO) {
		unsigned char *buf = buffer;

		for (bytes_read = 0; bytes_read < length; bytes_read++) {
			char key = input_getc();
			*buf++ = key;
			if (key == '\0')
				break;
		}
		//bytes_read = length;

		return bytes_read;
	}

	/* Standard Output */	
	else if (fd == STDOUT_FILENO) {
		return -1;
	}

	/* Normal File */
	else {
		struct file *file = find_file_by_fd (fd);

		/* File not exist */
		if (file == NULL) {
			return -1;
		}

		lock_acquire (&filesys_lock);
		bytes_read = file_read (file, buffer, length);
		lock_release (&filesys_lock);

		return bytes_read;
	}
}


/* Writes length bytes of data from buffer to open file fd 
   Return the number of bytes actually written 
   If fd is 1, it writes to the console using putbuf()
*/
int write (int fd, const void *buffer, unsigned length) {
	check_address (buffer);
	//check_address (buffer + length -1);

	int bytes_written;

	/* Standard Output */
	if (fd == STDOUT_FILENO) {
		putbuf (buffer, length);
		bytes_written = length;
		return bytes_written;
	}

	/* Standard Input */
	else if (fd == STDIN_FILENO) {
		return -1;
	}

	/* Normal file */
	else {
		struct file *file = find_file_by_fd (fd);

		if (file == NULL) {
			return -1;
		} 

		lock_acquire (&filesys_lock);
		bytes_written = file_write (file, buffer, length);
		lock_release (&filesys_lock);
		
		return bytes_written;
	}
	//return bytes_written;
}

/* Sets the current position in file to position bytes from the 
   start of the file 
   Gitbook : Changes the next byte to be read or written in open file fd to position,
   expressed in bytes from the beginning of the file
*/
void seek (int fd, unsigned position) {
	struct file *file = find_file_by_fd (fd);

	/* Ignore STDIN(value:1), STDOUT(value:2) */
	if (file <= 2) 
		return;

	file_seek (file, position);
}

/* Return the current position in file mapped with fd
   as a byte offset from the start of the file 
   Gitbook : Returns the position of the next byte to be read or written
   in open file fd, expressed in bytes from the beginning of the file
*/
unsigned tell (int fd) {
	struct file *file = find_file_by_fd (fd);

	/* Ignore STDIN(value:1), STDOUT(value:2) */
	if (file <= 2)
		return;

	off_t position = file_tell (file);
	return position;
}

/* Close file opened as fd */
void close (int fd) {
	struct file *close_file = find_file_by_fd (fd);

	/* If file not exists*/
	if (close_file == NULL) {
		return;
	}
	
	remove_file_from_fdt (fd);

	/* STDIN or STDOUT */
	if (fd <= 1 || close_file <= 2) 
		return;

	//lock_acquire (&filesys_lock);
	file_close (close_file);
	//lock_release (&filesys_lock);
}

/*Helper Functions*/
/*Add file to File Descriptor Table
  Return file descriptor on success, -1 on failure
*/
int add_file_to_fdt (struct file *file) {
	struct thread *curr = thread_current ();

	/*Find an empty entry*/
	while ((curr->next_fd < FDCOUNT_LIMIT) && (curr->fdt[curr->next_fd])) {
		curr->next_fd++;
	}

	/*If FDT is full, return -1*/
	if (curr->next_fd >= FDCOUNT_LIMIT) {
		return -1;
	}

	/* Add file into FDT */
	// curr->next_fd = fd;
	curr->fdt[curr->next_fd] = file;

	return curr->next_fd;
}

/*Search thread's fdt and return file struct pointer*/
struct file *find_file_by_fd (int fd) {
	struct thread *curr = thread_current ();

	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}

	return curr->fdt[fd];
}

/*Remove file from file descriptor table*/
void remove_file_from_fdt (int fd) {
	struct thread *curr = thread_current ();

	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return;
	}

	curr->fdt[fd] = NULL;
	//curr->next_fd = fd;
}
/*###################################################*/