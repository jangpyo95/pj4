#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

#define MAX_ARG 3

static void syscall_handler (struct intr_frame *);
void halt(void);
void exit(int status);
pid_t exec(const char *cmd_line);
int wait(pid_t pid);
void check_address(void *addr);
void get_argument(void *esp, int *arg, int count);
bool create(const char *file, unsigned size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);


//struct lock filesys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
   int arg[MAX_ARG];
   uint32_t *esp = f->esp;
   uint32_t *eax = &f->eax; 
   check_address(esp);   
  // printf(" syscall num:%d\n ",*esp);

  switch(*esp){

  case SYS_HALT:
   halt();
   break;

  case SYS_EXIT:
   get_argument(esp,arg,1);
   exit(arg[0]);
   break;

  case SYS_EXEC:
  {
   get_argument(esp,arg,1);
   check_address(arg[0]);
   *eax = exec((const char *)arg[0]);
   break;
  }

  case SYS_WAIT:
   get_argument(esp,arg,1);
   *eax = wait(arg[0]);
   break;

  case SYS_CREATE:
   get_argument(esp,arg,2);
   check_address(arg[0]);
   *eax = create((const char*) arg[0], arg[1]);
   break;

  case SYS_REMOVE:
   get_argument(esp,arg,1);
   *eax = remove((const char*)arg[0]);
   break;

  case SYS_OPEN:
   get_argument(esp,arg,1);
   check_address(arg[0]);
   *eax = open((const char*) arg[0]);
   //printf("im open: %d \n", *eax);
   break;

  case SYS_FILESIZE:
   get_argument(esp,arg,1);
   *eax = filesize(arg[0]);
   break;

  case SYS_READ:
   get_argument(esp,arg,3);
   check_address(arg[1]);
   check_address(arg[1]+arg[2]-1);
   //printf("i'm read pre:%d\n",arg[0]);
   *eax = read(arg[0],(void*)arg[1],(unsigned)arg[2]);

   //printf("read size: %d\n", *eax);
   break;

  case SYS_WRITE:
   get_argument(esp,arg,3);
   check_address(arg[1]);
   check_address(arg[1]+arg[2]-1);
   //printf("i'm write pre:%d\n", arg[0]);
   *eax = write(arg[0],(void*)arg[1],(unsigned)arg[2]);
   break;

  case SYS_SEEK:
   get_argument(esp,arg,2);
   seek(arg[0],(unsigned)arg[1]);
   break;

  case SYS_TELL:
   get_argument(esp,arg,1);
   *eax = tell(arg[0]);
   break;

  case SYS_CLOSE:
   get_argument(esp,arg,1);
   close(arg[0]);
   break;

  default:
   exit(-1);
  }
}

void halt(void){
  
  shutdown_power_off();
}

void exit(int status){

  struct thread *cur = thread_current();
  cur->exit_status = status; 
  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}


pid_t exec(const char *cmd_line){

  tid_t child_tid;
  struct thread *child;
  check_address(cmd_line);
 // printf("cmd_line: %s\n",cmd_line);
  child_tid = process_execute(cmd_line);
  if(child_tid == -1)
    return -1;
  child = get_child_process(child_tid);
  sema_down(&child->load_sema);
  if(!child->load)
   return -1;

  return child_tid;

}

int wait(pid_t pid){

  return process_wait(pid);
}


void check_address(void *addr){

  struct thread *cur = thread_current();
  if(!is_user_vaddr(addr) || addr<0x804800)
    exit(-1);
    
 // if(pagedir_get_page(cur->pagedir,addr)==NULL)
 //   exit(-1);
}

void get_argument(void *esp, int *arg, int count){
 
   int i;
   int *p;
   
   for(i=0; i<count; i++){
     p = (int*)esp+(i+1);
     check_address((void*)p);
     arg[i] = *p;
   }
}

bool create(const char *file, unsigned size){

    return filesys_create(file, size);
}

bool remove(const char *file){
       
     return filesys_remove(file);

}

int open(const char *file){
   
   struct file * f;
   f = filesys_open(file); 
   if(f==NULL) 
     return -1;

   return process_add_file(f);
}

int filesize(int fd){

   struct file *f = process_get_file(fd);
   if (f==NULL)
      return -1;
   
   return file_length(f);
}

int read(int fd, void *buffer, unsigned size){
   //printf("im'in read\n");
   lock_acquire(&filesys_lock);
   if(fd==0){
      unsigned n = size;
      while(n){
       *(char *)buffer = input_getc();
        (char *)buffer++;
        n--;
      }
      lock_release(&filesys_lock);
      return size;
   }
   
   struct file *f = process_get_file(fd);
   if(f == NULL){
      lock_release(&filesys_lock);
      return -1;
   }
   size = file_read(f,buffer,size);
   lock_release(&filesys_lock);
   return size;
}
 
int write(int fd, void *buffer, unsigned size){

   lock_acquire(&filesys_lock);
   if(fd==1){
      putbuf(buffer,size);
      lock_release(&filesys_lock);
      return size;
   }
   
   struct file *f = process_get_file(fd);
   size = file_write(f,buffer,size);
   lock_release(&filesys_lock);
   return size;
} 
   
void seek(int fd, unsigned position){

  struct file *f = process_get_file(fd);
  file_seek(f,position);
}

unsigned tell(int fd){

   struct file *f= process_get_file(fd);
   return file_tell(f);
}

void close(int fd){

   process_close_file(fd);
}
