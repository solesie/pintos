#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "threads/palloc.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "process.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

#ifdef VM
/* swap device의 데이터를 spte->kernel_virtual_page_in_user_pool에 올려둔다.
   성공 여부를 반환한다. proj4 pptx)Page Fault Handler 참조 */
static bool vm_load_SWAP_to_user_pool(struct supplemental_page_table_entry* spte){
  ASSERT(spte->frame_data_clue == SWAP);
  //Is there remaining?
  void* kernel_virtual_page_in_user_pool = vm_frame_allocate(PAL_USER, spte -> user_page);//Page replacement algorithm
  if(kernel_virtual_page_in_user_pool == NULL){
    PANIC("frame allocate 에러");
    return false;
  }

  //Swap page into frame from disk
  vm_swap_in(spte->swap_slot, kernel_virtual_page_in_user_pool);

  //Modify page and swap manage tables
  if(!install_page(spte->user_page, kernel_virtual_page_in_user_pool, spte->writable)) {
    PANIC("install_page 에러");
    vm_frame_free(kernel_virtual_page_in_user_pool);
    return false;
  }
  
  return true;
}

static bool grow_user_stack(void* faulted_page){
  void* kernel_virtual_page_in_user_pool = vm_frame_allocate(PAL_USER, faulted_page);
  if(kernel_virtual_page_in_user_pool == NULL){
    PANIC("frame allocate 에러");
    return false;
  }

  if(!install_page(faulted_page, kernel_virtual_page_in_user_pool, true)) {
    PANIC("install_page 에러");
    vm_frame_free(kernel_virtual_page_in_user_pool);
    return false;
  }
  
  return true;
}
#endif
/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference".
   

   4.3.3) %esp는 user pool을 가르키다가, interrupt가 발생하면 %esp는 kernel pool의 
   kernel stack을 가르킨다. 그리고 intr_frame 형태로 기존 user pool에서의 
   register 정보들을 %esp부터 저장한다. 즉, CPU는 user->kernel모드로 전환될 때만 
   기존 user pool에서의 register 정보들을 (intr_frame 형태로) kernel stack에 저장한다. 
   따라서 kernel에서 page fault가 발생했다면 intr_frame에 esp는 없다. */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;
#ifdef VM
  /* 3.1.5) system call에서 kernel은 user program이 넘겨준 포인터로 
     메모리에 엑세스 해야한다. kernel은 이때 매우 신중해야한다. 왜냐하면 user가

     1. null pointer를 넘길 수도 있고
     2. unmapped된 virtual memory에 접근하는 pointer를 넘길 수도 있고
     3. kernel space(above PHYS_BASE)에 접근하는 pointer를 넘길 수도 있다.

     이를 해결하기 위해 

     1. user pointer의 유효성을 검증한 후 user pointer로 메모리에 엑세스 하는 방법과
     2. 3번만 확인한 후 user pointer로 메모리에 엑세스 하는 방법이 있다.

     2번 방법을 선택할 경우 여기 page_fault()에서 invalid user pointer를 검증해야한다.
     그러나 이는 user->kernel mode로 바뀌고 kernel이 page fault를 발생시킨다.
     결국 user pool의 esp를 추적할 수 없게된다.

     어려우므로 그냥 1번 방법을 택한다. */

  /* user == false, 즉 kernel에 의해서 page fault된 경우를 디버깅해야한다.
     1번방법을 택했고 syscall에서 전부 다 처리했기 때문이다. */
  if(user == false){
    printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
    PANIC("kernel page fault - 아마도 kernel bug 일것");
    exit(-1);
  }

  struct thread* t = thread_current();
  void* faulted_user_page = pg_round_down(fault_addr);

  /* Is valid reference ?
     4.1.4) If the supplemental page table indicates that the user process
     should not expect any data at the address it was trying to access,
     or if the page lies within kernel virtual memory, 
     or if the access is an attempt to write to a read-only page, 
     then the access is invalid. Any invalid access terminates the process
     and thereby frees all of its resources. */
  struct supplemental_page_table_entry* spte = vm_spt_lookup(&t->spt, faulted_user_page);
  if(spte != NULL && !is_kernel_vaddr(faulted_user_page) && (not_present || !write)){
    //Call handle_mm_fault
    if(spte->frame_data_clue == SWAP && vm_load_SWAP_to_user_pool(spte)){
      pagedir_set_dirty (t->pagedir, spte->kernel_virtual_page_in_user_pool, false);
      //Restart process
      return;
    }
    /* 이 상황 디버그 필요 */
    printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  }

  /* No, then Growable region? 
     4.3.3) The 80x86 PUSH instruction checks access permissions before
     it adjusts the stack pointer, so it may cause a page fault 4 bytes 
     below the stack pointer. Similarly, the PUSHA instruction pushes 32 bytes 
     at once, so it can fault 32 bytes below the stack pointer.
     You should impose some absolute limit on stack size, as do most OSes. 
     Some OSes make the limit user-adjustable, e.g. with the ulimit command 
     on many Unix systems. On many GNU/Linux systems, the default limit is 8 MB. */
  void* user_esp = f->esp;
  bool is_normal_exceed = fault_addr == user_esp;
  bool is_push_instruction = fault_addr == user_esp - 4;
  bool is_pusha_instruction = fault_addr == user_esp - 32;
  bool is_in_limit_stack = PHYS_BASE - fault_addr <= 8*1024*1024;
  printf("%p %p\n",fault_addr, user_esp);
  if(is_in_limit_stack && (is_normal_exceed || is_push_instruction || is_pusha_instruction)){
    //Expand Userstack
    if(grow_user_stack(faulted_user_page)){
      //Restart process
      return;
    }
    /* 이 상황 디버그 필요 */
    printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  }

  /* No, then 죽임 */
  exit(-1);
#else
  if(user == false || is_kernel_vaddr(fault_addr) || not_present){
    exit(-1);
  }
  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
#endif
}

