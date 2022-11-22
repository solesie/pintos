#include <hash.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* frame table은 user page를 저장하고 있는 frame에 대한 정보를 저장한다.
   즉, 모든 프레임들을 저장하지 않고, per system이다.
   frame table entry를 빠르게 탐색할 수 있도록 해시테이블을 사용한다.
   (key, value) = (frame, frame_table_entry) */
static struct hash frame_table;

/* lock for frame_table */
static struct lock frame_table_lock;

static unsigned frame_table_hash_func(const struct hash_elem* e, void* aux);
static bool frame_table_less_func(const struct hash_elem* a, const struct hash_elem* b, void* aux);


struct frame_table_entry{
    /* 이 엔트리가 나타내는 frame을 kernel_virtual_page에 virtual address로 저장한다.
       이것이 가능한 이유는 PintOs는 64MB-Physical-memory를 vitual memory의 kernel space에 전부 1:1 매핑하기 때문이다.
       (user space는 page directory를 이용해 frame을 찾아갈 것이다.)

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

    void* kernel_virtual_page;  /* (In PintOs) kernel space of virtual memory는
                                    physical memory에 완벽히 1:1 대응되어 매핑된다. 즉, 이 변수가 frame이라 여기면 된다.
                                    kernel page of virtual memory 주소를 저장한다. */
    void* user_page;            /* kernel_virtual_page가 저장하는 user_page의 주소 */

    struct hash_elem elem;      /* see ::frame_table */

    struct thread *t;           /* 이 엔트리와 연관된 thread */

    bool important;             /* if true, never be evicted.
                                   The frame table allows Pintos to efficiently implement an eviction
                                   policy, by choosing a page to evict when no frames are free.
                                   This variable is the reason why frame table exists!! */
};

void vm_frame_init(){
  lock_init(&frame_table_lock);
  hash_init(&frame_table, frame_table_hash_func, frame_table_less_func, NULL);
}

/**
 * user page를 위한 새로운 frame을 할당하고 frame table에 기록한다. 
 * 새로운 kernel virtual page의 주소(새로운 frame의 주소와 1:1 매핑)를 반환한다.
 * @param flags frame에 user_page를 할당할때 줄 옵션들
*/
void* vm_frame_allocate (enum palloc_flags flags, void* user_page){
  lock_acquire (&frame_table_lock);
  void* kernel_virtual_page = palloc_get_page (PAL_USER | flags);
  if (kernel_virtual_page == NULL) { //빈 frame이 존재하지 않는 경우
    // swap 나중에 구현
    return NULL;
  }

  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
  if(fte == NULL) {
    return NULL;
  }

  fte->t = thread_current ();
  fte->user_page = user_page;
  fte->kernel_virtual_page = kernel_virtual_page;     /* 실제로 physical memory에도 반영하는 코드는...??
                                                         threads/start.S 참조. */
  fte->important = true;

  hash_insert (&frame_table, &fte->elem);

  lock_release(&frame_table_lock);
  return kernel_virtual_page;
}

/* 인자로 받은 frame을 frame table에서 없애고 free한다. */
void vm_frame_free (void* kernel_virtual_page){
  lock_acquire (&frame_table_lock);
  ASSERT(is_kernel_vaddr(kernel_virtual_page));
  
  /* frame table에서 kernel_virtual_page를 key로 가지는 frame_table_entry를 찾는다. */
  struct frame_table_entry temp;
  temp.kernel_virtual_page = kernel_virtual_page; // key 설정
  struct hash_elem* e = hash_find(&frame_table, &(temp.elem)); // 어짜피 hash_find는 kernel_virtual_page만 비교하도록 
                                                               // less함수를 설정하였다.
  ASSERT(e != NULL); // frame table에 존재하는 kernel-virtual-page(=frame)만 없앨 수 있다.
  struct frame_table_entry* fte = hash_entry(e, struct frame_table_entry, elem);

  hash_delete (&frame_table, &fte->elem);// frame table에서 kernel_virtual_page에 해당하는 frame table entry를 없앤다.
  palloc_free_page(kernel_virtual_page);// kernel space에서 kernel_virtual_page를 free한다.
  lock_release(&frame_table_lock);
}

/* hash function들 */
static unsigned frame_table_hash_func(const struct hash_elem *e, void* aux UNUSED)
{
  struct frame_table_entry* fte = hash_entry(e, struct frame_table_entry, elem);
  return hash_int((int)fte->kernel_virtual_page);
}
static bool frame_table_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct frame_table_entry *fte_a = hash_entry(a, struct frame_table_entry, elem);
  struct frame_table_entry *fte_b = hash_entry(b, struct frame_table_entry, elem);
  return fte_a->kernel_virtual_page < fte_b->kernel_virtual_page;
}