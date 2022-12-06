#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "vm/frame.h"
#include "vm/page.h"

#include "filesys/inode.h"
#include "threads/malloc.h"

#ifndef VM
#define vm_frame_allocate(x, y) palloc_get_page(x)
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static void construct_stack(const char* file_name, void** esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  //command file open
  char command[256];
  int i;
  strlcpy(command, file_name, strlen(file_name) + 1);
  for (i=0; command[i]!='\0' && command[i] != ' '; i++);
  command[i] = '\0';

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (command, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  else{
    struct thread *cur = thread_current ();
    struct list_elem *e = list_pop_back (&cur->child); // start_process 를 실행하는 자식 프로세스
    struct thread* child = list_entry (e, struct thread, child_elem);

    sema_down (&child->wait_sema); // 자식 프로세스의 load가 수행된 뒤에 수행될 수 있도록.
    if (!child->load_success)
      tid = TID_ERROR;
    else
      list_push_back (&cur->child, e);
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  struct thread* cur = thread_current ();
  cur->load_success = success;
  sema_up (&cur->wait_sema); // 현재 프로세스를 wait 할 수 있게 한다.

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  //부모는 항상 자식의 pid를 알고 있어야 한다. 자식이 끝날때까지 wait 해야한다.
  int exit_status;
  struct list_elem *element = list_begin(&(thread_current())->child);
  struct thread* t = NULL;
  while(element != list_end(&(thread_current()->child))){
    //list_entry()를 통해 list_elem 바깥쪽 데이터에 접근 가능한 pintos의 list 자료구조
    t = list_entry(element, struct thread, child_elem);
    list_remove(&(t->child_elem));
    if(child_tid == t->tid){
      if(t->status == THREAD_DYING){ //exit() 이 호출된 이후라면(부모가 종료되고...)
        if(t->exit_status == 0){ //정상적으로 종료된 경우
          exit_status = 0;
          sema_up(&t->exit_sema); //exit에서 sema_down으로 기다리고 있는 t가 exit 될 수 있도록 한다.
        }
        else{
          sema_up(&t->exit_sema);
          return -1;
        }
      }
      else{ //아직 exit() 이 호출되기 전이라면
        sema_down (&t->wait_sema); //exit에서 sema_up을 해줄 때 까지 기다린다.
        exit_status = t->exit_status;
        sema_up (&t->exit_sema);
      }
      return exit_status;
    }
    element = list_next(element);
  }
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* (PANIC 발생 시 예외) 이 스레드가 점유하고있는 global lock들을 해제해야한다. (implement later) */
#ifdef VM
  /* spt 메모리 해제 (implement later) */
  vm_spt_destroy(&cur->spt);
#endif

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* 이 프로세스가 종료될 때, 자식 프로세스는 모두 종료가 가능하게 된다.
     list 메모리를 해제한다. */
  while (!list_empty(&cur->child)) {
    struct list_elem *e = list_pop_front (&cur->child);
    struct thread *c = list_entry (e, struct thread, child_elem);
    sema_up (&c->exit_sema);
  }
  
  sema_up(&(cur->wait_sema)); // cur을 wait하고 있다면 실행될 수 있도록 한다.
  sema_down(&(cur->exit_sema)); // cur이 wait()를 통해 exit_sema를 올리거나, 부모가 종료되기 전까지 이 함수는 종료되지 않는다.(zombie)
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  //command file open
  char command[256];
  strlcpy(command, file_name, strlen(file_name) + 1);
  for (i=0; command[i]!='\0' && command[i] != ' '; i++);
  command[i] = '\0';

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
#ifdef VM
  /* spt 할당 */
  vm_spt_create(&(t->spt));
#endif
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (command);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", command);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", command);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
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
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
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
  if (!setup_stack (esp))
    goto done;
  /* file_name 에서 command를 파싱한 후, filesys_open(...) 으로 파일을 열었다.
   * 이제 file_name에서 setup user stack(construct stack)을 만든다.
   */
  construct_stack(file_name,esp); 

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}
static void construct_stack(const char* file_name, void** esp){
  //argc를 구한다.
  int argc = 0;
  char temp[256];
  char* next_ptr;
  
  strlcpy(temp, file_name, strlen(file_name) + 1);
  char* ret_ptr = strtok_r(temp, " ", &next_ptr);
  while (ret_ptr != NULL) {
    ++argc;
    ret_ptr = strtok_r(NULL, " ", &next_ptr);
  }
  //argv를 초기화한다.
  char** argv = (char **)malloc(sizeof(char*) * argc);
  strlcpy(temp, file_name, strlen(file_name) + 1);
  ret_ptr = strtok_r(temp, " ", &next_ptr);
  for (int i = 0; i < argc; ++i){
    argv[i] = ret_ptr;
    ret_ptr = strtok_r(NULL, " ", &next_ptr);
  }

  //esp를 이용하여 user stack을 초기화한다.
  int total_byte = 0; //for 4byte aligned
  for (int i = argc - 1; i >= 0; --i) {
    int byte = strlen(argv[i]);
    *esp = *esp - (byte + 1); //contain NULL
    total_byte += byte + 1;
    //*esp에 argv[i]를 복사한 후, argv[i] 는 그것을 참조하도록 한다. argv 는 free 할 것이기 때문.
    strlcpy(*esp, argv[i], byte + 1);
    argv[i] = *esp;
  }
  //4bytes aligned
  bool aligned = total_byte%4 == 0 ? true : false; 
  if(!aligned)
    *esp = *esp - (4 - total_byte % 4);
  //NULL을 의미하는 0
  *esp -= 4;
  **(uint32_t**)esp = (char*)0;
  //argv[argc - 1] 부터 argv[0] 까지의 주소를 삽입한다.
  for (int i = argc - 1; i >= 0; --i) {
    *esp -= 4;
    **(uint32_t**)esp = (char*)argv[i];
  }
  //argv의 주소를 삽입한다.
  *esp -= 4;
  **(uint32_t**)esp = *esp + 4;

  //[DEBUG] 
  //printf("%x == %p\n", **(uint32_t**)esp, *esp + 4);

  //argc 삽입
  *esp -= 4;
  **(uint32_t **)esp = argc;
  
  //return address는 0번으로 설정해준다.
  *esp -= 4;
  **(uint32_t **)esp = 0;

  free(argv);
  //[DEBUG]
  //hex_dump(*esp, *esp, 100, 1);
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
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

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      // 1. kernel virtual page(=frame)를 할당받는다.
      /* Get a page of memory. */
      uint8_t *kpage = vm_frame_allocate (PAL_USER, upage);
      if (kpage == NULL)
        return false;

      // 2. file의 내용을 읽어 kernel virtual page(=frame)에 쓴다.
      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
#ifdef VM
          struct supplemental_page_table_entry key;
          key.kernel_virtual_page_in_user_pool = kpage;
          key.user_page = upage;
          struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(&key);
          vm_frame_free(fte);
#else
          palloc_free_page(kpage);
#endif
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      // 3. upage의 logical address로 page-directory -> page-table을 따라 찾아가면 나오는 pte에
      //    kpage(frame)을 연관시킨다.
      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
#ifdef VM
          struct supplemental_page_table_entry key;
          key.kernel_virtual_page_in_user_pool = kpage;
          key.user_page = upage;
          struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(&key);
          vm_frame_free(fte);
#else
          palloc_free_page(kpage);
#endif
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  /* virtual memory는 프로세스마다 독립적이다.
     가상 주소는 *esp = PHYS_BASE, ((uint8_t *) PHYS_BASE) - PGSIZE 처럼 같지만,
     pagedir로 찾아가는 frame(여기서 kpage)는 프로세스마다 다르다.(= pte에 저장된 값이 서로 다르다)
     virtual memory의 가장 핵심적인 부분...(중간고사 마지막 문제 참조)

     프로세스마다 user space인 PHYS_BASE - PGSIZE에 한 페이지 크기의 스택을 마련한다. */
  kpage = vm_frame_allocate (PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else{
#ifdef VM
          struct supplemental_page_table_entry key;
          key.kernel_virtual_page_in_user_pool = kpage;
          key.user_page = PHYS_BASE - PGSIZE;
          struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(&key);
          vm_frame_free(fte);
#else
          palloc_free_page(kpage);
#endif
      }
    }
  return success;
}


/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails.
   pagedir상에는 upage가 not present여야 한다.

   @param kpage: page obtained from the user pool with palloc_get_page() */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  bool ret = (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));

#ifdef VM
  if(ret){
    vm_spt_install_IN_FRAME_page(&t->spt, upage, kpage, writable);
    pagedir_set_dirty(t->pagedir, kpage, false);
  }
#endif
  return ret;
}

bool
reinstall_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  bool ret = (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));

#ifdef VM
  if(ret)
    vm_spt_set_IN_FRAME_page(&t->spt, upage, kpage, writable);
#endif

  return ret;
}