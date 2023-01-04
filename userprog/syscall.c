#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
// System Call ----------------------------------------------------------------------
#include "threads/init.h" // halt 함수에서 power_off 함수를 이용하기 위해
#include "filesys/filesys.h" // 아래서 만들 함수에서 filesys.c에 있는 함수를 이용하기 위해
#include "threads/synch.h"
// #include <list.h>
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "devices/input.h"
// System Call ----------------------------------------------------------------------

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// System Call ----------------------------------------------------------------------
// 함수 선언

void check_addr(const uint64_t *addr);
int add_file_to_fdt(struct file *file);
struct file *find_file_by_fd(int fd);
void remove_file_from_fdt(int fd);

void halt(void);
void exit(int status);
int wait(tid_t pid);
int exec(const char *file);
tid_t fork(const char *name, struct intr_frame *f);

bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
void close(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
int filesize(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);

struct lock filesys_lock;
// System Call ----------------------------------------------------------------------


/* System call.
 *
 * Previously system call services was handled by the interrupt handlerss
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

// System Call ----------------------------------------------------------------------
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
// System Call ----------------------------------------------------------------------

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
	// System Call ----------------------------------------------------------------------
	lock_init(&filesys_lock);
	// System Call ----------------------------------------------------------------------
}

/* The main system call interface */
void syscall_handler (struct intr_frame *f UNUSED) {
	// 시스템 콜 핸들러에 의해 해당 시스템 콜 함수와 연결된다.
	//printf ("system call!\n"); 나중에 테스트 통과에 영향을 주니깐 주석 처리

	// System Call ----------------------------------------------------------------------
	int sys_num = f->R.rax; // 시스템 콜 넘버

	// switch문을 통해 해당 시스템 콜로 갈 수 있게 해준다.
	switch(sys_num){
		// pintos를 종료 시키는 시스템 콜
		case SYS_HALT:
			halt();
			break;
		// 현재 스레드를 종료하는 시스템 콜
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		// 첫 인자를 이름으로 두번째 인자만큼의 바이트 크기의 파일 생성
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		// 인자와 같은 이름인 파일을 삭제
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		// 인자와 같은 이름인 파일을 열 때
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		// 해당 인자에 대한 파일을 닫는다.
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		// 해당 fd로 열린 파일에서 주어진 바이트 크기로 버퍼에서 읽는다.
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		// 해당 fd 파일에 buffer에서 size 바이트 만큼 쓴다.
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		// 해당 fd로 열린 파일의 바이트 크기를 반환하는 함수
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		// 해당 fd의 다음 읽거나 쓸 위치를 넘겨준 인자로 바꾸어 준다.
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		// 해당 fd의 다음 읽거나 쓸 위치를 반환해준다.
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		// 부모가 자식을 기다려야 할때 대기시키는 함수
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		// 현재 프로세스의 자리를 새로운 프로세스에게 양도?
		case SYS_EXEC:
			if (exec(f->R.rdi) == -1){
				exit(-1);
			}
			break;
		// 자식 프로세스 생성
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		default:
			exit(-1);
	}
	// System Call ----------------------------------------------------------------------

	// thread_exit (); 나중에 테스트 통과에 영향을 준다.
}

// System Call ----------------------------------------------------------------------
// 함수 구현

void halt(void){
	// power_off 함수를 통해 pintos 종료
	power_off();
}

void exit(int status){
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, t->exit_status);
	thread_exit(); // 스레드 종료
}

int wait(tid_t pid){
	return process_wait(pid);
}

tid_t fork(const char *name, struct intr_frame *f){
	return process_fork(name, f);
}

int exec(const char *file){
	check_addr(file);

	int size = strlen(file) + 1;
	char *copy_file = palloc_get_page(PAL_ZERO);

	if (copy_file == NULL){
		exit(-1);
	}
	strlcpy(copy_file, file, size);

	if (process_exec(copy_file) == -1){
		return -1;
	}

	NOT_REACHED();

	return 0;
}

bool create(const char *file, unsigned initial_size){
	check_addr(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file){
	check_addr(file);
	return filesys_remove(file);
}

int open(const char *file){
	check_addr(file);

	lock_acquire(&filesys_lock);
	struct file *open_file = filesys_open(file);
	lock_release(&filesys_lock);

	// 파일이 제대로 생성되었는지 확인
	if (open_file == NULL){
		return -1;
	}

	// 해당 이름의 파일을 현재 스레드 fdt에 추가
	int fd = add_file_to_fdt(open_file);

	// 만약 파일을 열수 없다면 다시 닫는다?
	if (fd == -1){
		file_close(open_file);
	}
	return fd;
}

void close(int fd){
	struct file *close_file = find_file_by_fd(fd);
	if (close_file == NULL){
		return;
	}
	remove_file_from_fdt(fd);
	
	if (fd < 2 || close_file <= 2){
		return;
	}
	// lock_acquire(&filesys_lock);
 	file_close(close_file);
	//lock_release(&filesys_lock);
 }

int read(int fd, void *buffer, unsigned size){
	// 버퍼의 시작과 끝이 유효 주소인지 확인
	check_addr(buffer);
	check_addr(buffer + size -1);

	unsigned char *buf = buffer;
	int byte_count;

	if (fd == STDIN_FILENO){
		char key;
		for (byte_count = 0; byte_count < size; byte_count++){
			key = input_getc();
			*buf++ = key;
			if (key == '\0'){
				break;
			}
		}
	}
	else if (fd == STDOUT_FILENO){
		return -1;
	}
	else{
		struct file *read_file = find_file_by_fd(fd);

		if (read_file == NULL){
			return -1;
		}

		lock_acquire(&filesys_lock);
		byte_count = file_read(read_file, buffer, size);
		lock_release(&filesys_lock);
	}
	return byte_count;
}

int write(int fd, const void *buffer, unsigned size){
	check_addr(buffer);
	struct file *write_file = find_file_by_fd(fd);
	int byte_count;
	
	if (write_file == NULL){
		return -1;
	}

	if (fd == STDOUT_FILENO){
		putbuf(buffer, size);
		byte_count = size;
	}
	else if (fd == STDIN_FILENO){
		return -1;
	}
	else{
		lock_acquire(&filesys_lock);
		byte_count = file_write(write_file, buffer, size);
		lock_release(&filesys_lock);
	}
	return byte_count;
}

int filesize(int fd){
	struct file *file = find_file_by_fd(fd);
	if (file == NULL){
		return -1;
	}

	return file_length(file);
}

void seek(int fd, unsigned position){
	struct file *file = find_file_by_fd(fd);
	
	if (file <= 2){
		return;
	}

	file_seek(file, position);
}

unsigned tell(int fd){
	struct file *file = find_file_by_fd(fd);
	
	if (file <= 2){
		return;
	}

	return file_tell(file);
}

// System Call ----------------------------------------------------------------------

// System Call ----------------------------------------------------------------------
// 추가 함수

// 해당 주소가 유효 주소인지 체크하는 함수
void check_addr(const uint64_t *addr){

	struct thread *t = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(t->pml4, addr) == NULL){
		exit(-1);
	}
}

// 파일을 현재 프로세스의 file_dt(파일 디스크립트 테이블)에 파일을 추가하는 함수
int add_file_to_fdt(struct file *file){
	struct thread *t = thread_current();
	struct file **fdt = t->file_dt;

	// fdt에 빈 자리가 날 때까지 fd 값을 계속 1씩 올린다.
	while (fdt[t->fdidx] != NULL && t->fdidx < FDT_COUNT_LIMIT){
		t->fdidx++;
	}


	if (t->fdidx >= FDT_COUNT_LIMIT){
		return -1;
	}

	// 해당 배열 인덱스에 파일을 배치하고 해당 fdt 인덱스 값을 반환
	fdt[t->fdidx] = file;
	return t->fdidx;
}

// 현재 실행중인 스레드의 fdt에서 해당 fd에 있는 file주소 리턴
struct file *find_file_by_fd(int fd){

	if (fd < 0 || fd >= FDT_COUNT_LIMIT){
		return NULL;
	}
	struct thread *t = thread_current();
	struct file **fdt = t->file_dt;
	struct file *file = fdt[fd];

	return file;
}

// 현재 실생중인 스레드의 fdt에서 해당 fd에 있는 값을 NULL로 초기화
void remove_file_from_fdt(int fd){
	struct thread *t = thread_current();

	if (fd < 0 || fd >= FDT_COUNT_LIMIT){
		return;
	}
	t->file_dt[fd] = NULL;
}

// System Call ----------------------------------------------------------------------