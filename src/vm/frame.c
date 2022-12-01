#include <hash.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

/* frame table은 user page를 저장하고 있는 frame에 대한 정보를 저장한다.
   즉, 모든 프레임들을 저장하지 않고, per system이다.
   frame table entry를 빠르게 탐색할 수 있도록 해시테이블을 사용한다.
   (key, value) = (frame, frame_table_entry)
                                                        (unused) 0xFFFFFFFF ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
0x04000000 +----------------------------------+   <<------>>  0xC4000000 +----------------------------------+
           |                                  |   one-to one             |                                  |
           |            page pool             |                          |                                  |             
           |             (63 MB)              |                          |                                  |
           |                                  |                          |                                  |
           |                                  |                          |                                  |
           |                                  |                          |                                  |
           |                                  |                          |                                  |
           |                                  |                          |                                  |
           |                                  |                          |                                  |    
0x00100000 +----------------------------------+                          |           kernel space           | 
           |                                  |                          |                                  |
           |                                  |                          |                                  |
0x00007E00 +----------------------------------+                          |                                  |
           |            Boot loader           |                          |                                  |
0x00007C00 +----------------------------------+                          |                                  |
           |                                  |                          |                                  |
           |                                  |   one-to one             |                                  |
         0 +----------------------------------+   <<------>>  0xC0000000 +----------------------------------+ PHYS_BASE
                                                        (user space) 0x0  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
              <PintOs Physical memory(64mb)>                                  <PintOs Virtual memory(4GB)> */
static struct hash frame_table;

/* lock for frame_table */
static struct lock frame_table_lock;

static unsigned frame_table_hash_func(const struct hash_elem* e, void* aux);
static bool frame_table_less_func(const struct hash_elem* a, const struct hash_elem* b, void* aux);


struct frame_table_entry{
    /* 이 엔트리가 나타내는 frame을 kernel_virtual_page에 virtual address로 저장한다.
       이것이 가능한 이유는 PintOs는 64MB-Physical-memory를 vitual memory의 kernel space에 전부 1:1 매핑하기 때문이다.
       (user space는 page directory를 이용해 frame을 찾아갈 것이다.)
       
       (In PintOs) kernel space of virtual memory는
       physical memory에 완벽히 1:1 대응되어 매핑된다. 즉, 이 변수가 frame이라 여기면 된다.
       kernel page of virtual memory 주소를 저장한다. */
    void* kernel_virtual_page_in_user_pool;

    void* user_page;            /* kernel_virtual_page가 저장하는 user_page의 주소. alias. */

    struct hash_elem elem;      /* see ::frame_table */

    struct thread *t;           /* 이 엔트리와 연관된 thread */

    /* if true, never be evicted.
       The frame table allows Pintos to efficiently implement an eviction
       policy, by choosing a page to evict when no frames are free.
       This variable is one of the reason why frame table exists!!
       
       4.3.5) eviction 구현 이후, User pointer가 커널 코드에 의해 액세스 되는 동안에도 evict되어 
       kernel 모드에서 page fault가 발생한다. 커널은 “page fault를 해결하는데 필요한 리소스를 보유하고 있는 동안”
       에는 이 page fault를 방지해야한다.

       page fault를 해결하는데 필요한 리소스에는 device driver가 보유한 lock같은 것이 있다. 
       device driver는 swap device, fs device를 컨트롤하는 역할을 한다.
       (page fault에서 file_read, file_write로 swap device와 데이터를 주고받는 것을 볼 수 있다)

       즉, 커널이 read, write syscall로 file_read, file_write로 fs device와 데이터를 주고받을때 
       커널은 page fault를 해결하는데 필요한 리소스를 보유하고 있다. 이때 page fault를 방지해야한다. */
    bool is_used_for_user_pointer;

};

void vm_frame_init(){
  lock_init(&frame_table_lock);
  hash_init(&frame_table, frame_table_hash_func, frame_table_less_func, NULL);
}


struct frame_table_entry* vm_frame_lookup(void* kernel_virtual_page_in_user_pool){
  struct frame_table_entry key;
  key.kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;
  struct hash_elem* e = hash_find(&frame_table, &(key.elem));
  ASSERT(e != NULL);
  return hash_entry(e, struct frame_table_entry, elem);
}


/* random frame replacement algorithm
   user pointer로 참조되는 frame이 아닌 다른 frame을 아무거나 하나 골라서 반환한다. */
static uint32_t next = 1;
static struct frame_table_entry* pick_frame_to_evict(void){
  size_t n = hash_size(&frame_table);
  while(true){
    next = next*1103515245 + 12345;
    size_t pointer = next % n;

    struct hash_iterator it; 
    hash_first(&it, &frame_table);
    size_t i; for(i=0; i<=pointer; ++i) hash_next(&it);

    struct frame_table_entry *e = hash_entry(hash_cur(&it), struct frame_table_entry, elem);
    if(e->is_used_for_user_pointer) { 
      continue;
    }
    else return e;
  }
}


/* swap.c:vm_swap_out() */
static void vm_evict_a_frame_to_swap_device(){
  struct frame_table_entry* f_tobe_evicted = pick_frame_to_evict();

  //f_tobe_evicted의 내용을 swap devide에 기록한다.
  size_t swap_idx = vm_swap_out(f_tobe_evicted->kernel_virtual_page_in_user_pool);

  //f_tobe_evicted의 정보와 일치하는 spte를 찾아서 갱신한다.
  struct supplemental_page_table_entry* spte = vm_spt_lookup(&f_tobe_evicted->t->spt, f_tobe_evicted->user_page);
  vm_spt_update_after_swap_out(spte, swap_idx);

  //pagedir 에서 f_tobe_evicted의 정보를 없앤다.
  pagedir_clear_page(f_tobe_evicted->t->pagedir, f_tobe_evicted->user_page);

  hash_delete (&frame_table, &f_tobe_evicted->elem);// frame table에서 kernel_virtual_page에 해당하는 frame table entry를 없앤다.
  palloc_free_page(f_tobe_evicted->kernel_virtual_page_in_user_pool);
}


/* user pool에서 없으면 swap을 해서라도 frame을 반환하는 palloc_get_page() wrapper함수이다. */
static void* vm_super_palloc_get_page(enum palloc_flags flags){
  void* kernel_virtual_page_in_user_pool = palloc_get_page (PAL_USER | flags);
  if (kernel_virtual_page_in_user_pool == NULL) { 
    vm_evict_a_frame_to_swap_device();
    kernel_virtual_page_in_user_pool = palloc_get_page (PAL_USER | flags);
  }
  return kernel_virtual_page_in_user_pool;
}


/* 새로운 frame table entry를 frame table에 할당하는 함수이다. */
static void vm_add_fte(void* kernel_virtual_page_in_user_pool, void* user_page){
  struct frame_table_entry* fte = malloc(sizeof(struct frame_table_entry));

  fte->t = thread_current ();
  fte->user_page = user_page;
  fte->kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool; /* 실제로 physical memory에도 반영하는 코드는...??
                                                                               threads/start.S 참조. */
  fte->is_used_for_user_pointer = false;

  hash_insert (&frame_table, &fte->elem);
}


/* user page를 위한 새로운 frame을 user pool에서 할당하고 frame table에 기록한다. 
   새로운 kernel virtual page의 주소(새로운 frame의 주소와 1:1 매핑)를 반환한다.
   
   eviction을 구현한 후 이 함수를 사용하는 곳에서는 synchronize를 구현해야한다.(implement later)
   
   예를들어 일반적으로 f = vm_frame_allocte()를 한 다음 조건에 맞지 않다면 vm_free_allocate(f)를 호출한다.
   p1이 f를 구해서 조건을 확인하는 중이라 하자.
   p2가 f를 eviction한다면..?
   
   예를들어 p1, p2가 이 함수를 동시에 호출한다 하자.
   p1이 palloc으로 빈프레임을 할당받는데 성공하고, p2는 실패한다고 하자.
   p2가 방금 p1이 받은 프레임을 eviction한다면..?
   
   즉, palloc으로 받은 프레임으로 사용을 완료할 때 까지는 절대로 eviction되면 안된다. */
void* vm_frame_allocate (enum palloc_flags flags, void* user_page){
  lock_acquire(&frame_table_lock);
  void* kernel_virtual_page_in_user_pool = vm_super_palloc_get_page(flags);

  vm_add_fte(kernel_virtual_page_in_user_pool, user_page);
  lock_release(&frame_table_lock);
  return kernel_virtual_page_in_user_pool;
}


/* 인자로 받은 fte을 frame table에서 없애고 physical memory 상에서 free한다. */
void vm_frame_free (struct frame_table_entry* fte){
  lock_acquire(&frame_table_lock);
  hash_delete (&frame_table, &fte->elem);// frame table에서 kernel_virtual_page에 해당하는 frame table entry를 없앤다.
  palloc_free_page(fte->kernel_virtual_page_in_user_pool);
  lock_release(&frame_table_lock);
}


/* user program이 종료될 때 pagedir_destory()가 호출되면서 실제 physical memory에서 free된다. 
   이때 frame_table에서는 따로 지워주어야한다. */
void vm_frame_free_only_in_ft(struct frame_table_entry* fte){
  lock_acquire(&frame_table_lock);
  hash_delete (&frame_table, &fte->elem);// frame table에서 kernel_virtual_page에 해당하는 frame table entry를 없앤다.
  lock_release(&frame_table_lock);
}


/* setter */
void vm_frame_set_for_user_pointer(struct frame_table_entry* fte, bool value){
  lock_acquire(&frame_table_lock);
  fte->is_used_for_user_pointer = value;
  lock_release(&frame_table_lock);
}


/* hash function들 */
static unsigned frame_table_hash_func(const struct hash_elem *e, void* aux UNUSED)
{
  struct frame_table_entry* fte = hash_entry(e, struct frame_table_entry, elem);
  return hash_int((int)fte->kernel_virtual_page_in_user_pool);
}
static bool frame_table_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct frame_table_entry *fte_a = hash_entry(a, struct frame_table_entry, elem);
  struct frame_table_entry *fte_b = hash_entry(b, struct frame_table_entry, elem);
  return fte_a->kernel_virtual_page_in_user_pool < fte_b->kernel_virtual_page_in_user_pool;
}