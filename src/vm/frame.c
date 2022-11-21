#include <hash.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/synch.h"

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
    /* 이 엔트리가 나타내는 frame을 virtual address로 저장한다.
       이것이 가능한 이유는 PintOs는 64MB-Physical-memory를 vitual memory의 kernel space에 전부 1:1 매핑하기 때문이다.

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
                                    physical memory에 완벽히 1:1 대응되어 매핑된다.
                                    kernel page of virtual memory 주소를 저장한다. */

    struct hash_elem elem;      /* see ::frame_table */

    void* user_page;            /* kernel_virtual_page가 저장하는 user_page의 주소 */
    struct thread *t;           /* 이 엔트리와 연관된 thread */

    bool important;             /* if true, never be evicted. */
};

void vm_frame_init(){
  lock_init(&frame_table_lock);
  hash_init(&frame_table, frame_table_hash_func, frame_table_less_func, NULL);
}

/**
 * 새로운 frame을 할당하고, 그 frame이 나타내는 user virtual page의 주소를 반환한다.
 * @param flags frame에 user_page를 할당할때 줄 옵션들
 * @param user_page 할당할 user page
*/
void* vm_frame_allocate (enum palloc_flags flags, void* user_page){
  void* kernel_virtual_page = palloc_get_page (PAL_USER | flags);
  if (kernel_virtual_page == NULL) {
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

  hash_insert (&frame_table, &fte->elem);

  return kernel_virtual_page;
}