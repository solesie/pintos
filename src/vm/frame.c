#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"

#include "vm/frame-table-hash.h"

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
static struct vm_ft_hash frame_table;

/* reader-writers for frame_table

   x frame을 swap out이 안되게 고정하는 도중에 x frame이 swap out이 일어나면 안된다.
   frame number을 이용한 배열을 만들어서 frame 마다 lock을 잡으면 성능상 더 빠를수도 있다.
   그러나 시간상의 문제로 이는 구현하지 않는다. 대신 frame_table_w를 사용한다. */
static struct semaphore frame_table_w;
static int read_cnt;
static struct lock mutex;


static unsigned frame_table_hash_func(const struct vm_ft_hash_elem* e, void* aux);
static bool frame_table_less_func(const struct vm_ft_hash_elem* a, const struct vm_ft_hash_elem* b, void* aux);
static bool frame_table_value_less_func(const struct vm_ft_hash_elem*a, const struct vm_ft_hash_elem*b, void *aux);


struct frame_table_entry{
    /* 이 엔트리가 나타내는 frame을 kernel_virtual_page에 virtual address로 저장한다.
       이것이 가능한 이유는 PintOs는 64MB-Physical-memory를 vitual memory의 kernel space에 전부 1:1 매핑하기 때문이다.
       (user space는 page directory를 이용해 frame을 찾아갈 것이다.)
       
       (In PintOs) kernel space of virtual memory는
       physical memory에 완벽히 1:1 대응되어 매핑된다. 즉, 이 변수가 frame이라 여기면 된다.
       kernel page of virtual memory 주소를 저장한다. */
    void* kernel_virtual_page_in_user_pool;

    void* user_page;            /* kernel_virtual_page가 저장하는 user_page의 주소. alias. */

    struct vm_ft_hash_elem elem;      /* see ::frame_table */

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
    int is_used_for_user_pointer;

    bool setting_now;
};



void vm_frame_init(){
  lock_init(&mutex);
  sema_init(&frame_table_w, 1);
  read_cnt = 0;

  vm_ft_hash_init(&frame_table, frame_table_hash_func, frame_table_less_func, frame_table_value_less_func, NULL);
}

/* 이 함수 사용후 반환값을 vm_ft_same_keys_free를 통해 해제해야 한다. */
struct vm_ft_same_keys* vm_frame_lookup_same_keys(void* kernel_virtual_page_in_user_pool){
  lock_acquire(&mutex);
  ++read_cnt;
  if(read_cnt == 1)
    sema_down(&frame_table_w);
  lock_release(&mutex);

  struct frame_table_entry key;
  key.kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;
  struct vm_ft_same_keys* founds = vm_ft_hash_find_same_keys(&frame_table, &(key.elem));

  lock_acquire(&mutex);
  --read_cnt;
  if(read_cnt == 0)
    sema_up(&frame_table_w);
  lock_release(&mutex);
  return founds;
}


/* 정확히 spte의 내용과 일치하는 frame_table_entry 하나를 반환한다. */
struct frame_table_entry* vm_frame_lookup_exactly_identical(struct supplemental_page_table_entry* spte){
  lock_acquire(&mutex);
  ++read_cnt;
  if(read_cnt == 1)
    sema_down(&frame_table_w);
  lock_release(&mutex);

  ASSERT(spte->frame_data_clue == IN_FRAME);
  struct frame_table_entry key;
  key.kernel_virtual_page_in_user_pool = spte->kernel_virtual_page_in_user_pool;
  key.user_page = spte->user_page;
  struct vm_ft_hash_elem* e = vm_ft_hash_find_exactly_identical(&frame_table, &(key.elem));
  struct frame_table_entry* ret = vm_ft_hash_entry(e, struct frame_table_entry, elem);

  lock_acquire(&mutex);
  --read_cnt;
  if(read_cnt == 0)
    sema_up(&frame_table_w);
  lock_release(&mutex);
  return ret;
}


static bool can_be_evicted(struct vm_ft_same_keys* founds){
  for(int i = 0; i < founds->len; ++i){
      struct frame_table_entry *e = vm_ft_hash_entry(founds->pointers_arr_of_ft_hash_elem[i], struct frame_table_entry, elem);
      if(e->is_used_for_user_pointer > 0 || e->setting_now)
        return false;
  }
  return true;
}


/* random frame replacement algorithm
   user pointer로 참조되는 frame이 아닌 다른 frame을 아무거나 하나 골라서 반환한다. */
static uint32_t next = 1;
static struct vm_ft_same_keys* pick_frame_to_evict(void){
  size_t n = vm_ft_hash_size(&frame_table);

  while(true){
    next = next*1103515245 + 12345;
    size_t pointer = next % n;

    struct vm_ft_hash_iterator it; 
    vm_ft_hash_first(&it, &frame_table);
    size_t i; for(i=0; i<=pointer; ++i) vm_ft_hash_next(&it);

    struct vm_ft_same_keys* founds = vm_ft_hash_find_same_keys(&frame_table, vm_ft_hash_cur(&it));
    ASSERT(founds != NULL);
    if(can_be_evicted(founds))
      return founds;
  }
}


/* swap.c:vm_swap_out() */
static void vm_evict_a_frame_to_swap_device(void){
  struct vm_ft_same_keys* founds = pick_frame_to_evict();
  //frame table에서 evict할 frame을 모두 없앤다.
  struct vm_ft_same_keys* removed = vm_ft_hash_delete_same_keys (&frame_table, founds->pointers_arr_of_ft_hash_elem[0]);
  vm_ft_same_keys_free(founds);

  //내용을 swap devide에 기록한다.
  struct frame_table_entry* one_of_removed = vm_ft_hash_entry(removed->pointers_arr_of_ft_hash_elem[0], struct frame_table_entry, elem);
  size_t swap_idx = vm_swap_out(one_of_removed->kernel_virtual_page_in_user_pool, removed->len);

  void* kpage = one_of_removed->kernel_virtual_page_in_user_pool;

  for(int i = 0; i < removed->len; ++i){ //그러나 user pool sharing 구현은 없을 것
    struct frame_table_entry* fte = vm_ft_hash_entry(removed->pointers_arr_of_ft_hash_elem[i], struct frame_table_entry, elem);

    //fte의 정보와 일치하는 spte를 찾아서 갱신한다.
    struct supplemental_page_table_entry* spte = vm_spt_lookup(&fte->t->spt, fte->user_page);
    vm_spt_update_after_swap_out(spte, swap_idx);

    /* alias문제가 발생한다. user data를 kernel space address를 통해서도 접근해왔으므로,
       pagedir에 access bit, dirty bit가 서로 동기화되어있지 않다.
       access bit는 random evict algorithm을 적용했으므로 상관 쓸 필요가 없지만,
       dirty bit는 mmap시 정확한 정보가 필요하다. 
       둘중 하나라도 수정된 상황이면 수정했다고 여기고 수정했다는 정확한 정보를 저장한다. */
    pagedir_set_dirty(fte->t->pagedir, fte->user_page
      , pagedir_is_dirty(fte->t->pagedir, fte->user_page) || pagedir_is_dirty(fte->t->pagedir, fte->kernel_virtual_page_in_user_pool));
    pagedir_set_dirty(fte->t->pagedir, fte->kernel_virtual_page_in_user_pool, false);
    
    //pagedir에서 present bit 갱신
    pagedir_clear_page(fte->t->pagedir, fte->user_page);
    free(fte);
  }
  palloc_free_page(kpage);//physical memory에서 이 frame을 없앤다.
  vm_ft_same_keys_free(removed);
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
  fte->is_used_for_user_pointer = 0;
  fte->setting_now = true;

  vm_ft_hash_insert (&frame_table, &fte->elem);
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
  sema_down(&frame_table_w);

  void* kernel_virtual_page_in_user_pool = vm_super_palloc_get_page(flags);
  vm_add_fte(kernel_virtual_page_in_user_pool, user_page);

  sema_up(&frame_table_w);
  return kernel_virtual_page_in_user_pool;
}

/* ONLY USE IN vm_load_IN_SWAP_to_user_pool(), vm_load_IN_FILE_to_user_pool() */
static void* vm_frame_allocate_unsafe (enum palloc_flags flags, void* user_page){
  void* kernel_virtual_page_in_user_pool = vm_super_palloc_get_page(flags);
  vm_add_fte(kernel_virtual_page_in_user_pool, user_page);
  return kernel_virtual_page_in_user_pool;
}


/* 만약 fte->kernel_virtual_page_in_user_pool이 frame table에서 유일하다면,
   fte가 나타내는 frame을 deallocate한다.
   그리고 fte와 정확히 일치하는 데이터를 frame table에서 없앤다. */
void vm_frame_free (struct frame_table_entry* fte){
  sema_down(&frame_table_w);
  
  /* same as vm_frame_lookup_same_keys() */
  struct frame_table_entry key;
  key.kernel_virtual_page_in_user_pool = fte->kernel_virtual_page_in_user_pool;
  struct vm_ft_same_keys* others = vm_ft_hash_find_same_keys(&frame_table, &(key.elem));

  ASSERT(others != NULL);
  if(others->len == 1)//no sharing
    palloc_free_page(fte->kernel_virtual_page_in_user_pool);
  
  vm_ft_hash_delete_exactly_identical (&frame_table, &fte->elem);
  vm_ft_same_keys_free(others);
  free(fte);

  sema_up(&frame_table_w);
}


/* user program이 종료될 때 pagedir_destory()가 호출되면서 실제 physical memory에서 free된다. 
   이때 frame_table에서는 따로 지워주어야한다.
   fte의 정보와 정확히 일치하는 데이터를 frame_table에서 없애고 fte를 deallocate한다. */
void vm_frame_free_only_in_ft(struct frame_table_entry* fte){
  sema_down(&frame_table_w);

  vm_ft_hash_delete_exactly_identical (&frame_table, &fte->elem);
  free(fte);

  sema_up(&frame_table_w);
}


/* setter */
static void vm_frame_set_for_user_pointer(struct vm_ft_same_keys* founds, bool value){
  for(int i = 0; i < founds->len; ++i){
    struct frame_table_entry* fte = vm_ft_hash_entry(founds->pointers_arr_of_ft_hash_elem[i], struct frame_table_entry, elem);
    ASSERT(fte->is_used_for_user_pointer >= 0);
    if(value)
      ++fte->is_used_for_user_pointer;
    else if(!value && fte->is_used_for_user_pointer > 0)
      --fte->is_used_for_user_pointer;
  }
}

/* vm_frame_allocate(),
   vm_load_IN_FILE_to_user_pool,
   vm_load_IN_SWAP_to_user_pool 는 새로운 프레임을 할당한다.
   새로운 프레임에 대한 설정이 완료(= evict해도 된다는 의미)되면 이 함수를 호출한다. */
void vm_frame_setting_over(struct vm_ft_same_keys* founds){
  sema_down(&frame_table_w);

  for(int i = 0; i < founds->len; ++i){
    struct frame_table_entry* fte = vm_ft_hash_entry(founds->pointers_arr_of_ft_hash_elem[i], struct frame_table_entry, elem);
    fte->setting_now = false;
  }

  sema_up(&frame_table_w);
}


/* 4.3.5) user_pointer_inclusive부터 bytes만큼 physical memory에 고정시킨다.
   BLOCK_FILESYS, BLOCK_SWAP 와 같은 block device를 컨트롤하는 block device driver가 있다.
   BLOCK_FILESYS에서 block_read(), block_write()를 호출하는 도중에 page_fault()가 발생하여
   BLOCK_SWAP에서 block_read(), block_write()를 호출하는 상황이 발생하면 안된다.
   
   user_pointer_inclusive 부터 bytes까지 swap device에 존재하는 페이지는 physical memory로 데려온다.
   use in syscall.c */
void make_user_pointer_in_physical_memory(void* user_pointer_inclusive, size_t bytes){
  struct thread* t = thread_current();
  
  void* new_page = NULL;
  for(size_t i = 0; i < bytes; ++i){
      //페이지가 달라진 경우
      if(new_page != pg_round_down(user_pointer_inclusive + i)){
        //advanced
        new_page = pg_round_down(user_pointer_inclusive + i);

sema_down(&frame_table_w);

        /* 이번 user_pointer_inclusive + i가 속하는 페이지의 spte를 구한다. */
        struct supplemental_page_table_entry* spte = vm_spt_lookup(&t->spt, new_page);
        bool newly_allocated = false;


        if(spte->frame_data_clue == IN_SWAP){

          // same mechanism as  vm_load_IN_SWAP_to_user_pool (spte);
          void* kernel_virtual_page_in_user_pool = vm_frame_allocate_unsafe(PAL_USER, spte -> user_page);
          ASSERT(kernel_virtual_page_in_user_pool != NULL);
          vm_swap_in(spte->swap_slot, kernel_virtual_page_in_user_pool);
          ASSERT(reinstall_page(spte->user_page, kernel_virtual_page_in_user_pool, spte->writable));
          // vm_load_IN_SWAP_to_user_pool (spte) over

          newly_allocated = true;
        }


        if(spte->frame_data_clue == IN_FILE){

          // same mechanism as vm_load_IN_FILE_to_user_pool(spte);
          void* kernel_virtual_page_in_user_pool = vm_frame_allocate_unsafe(PAL_USER, spte -> user_page);
          ASSERT(kernel_virtual_page_in_user_pool != NULL);
          file_seek(spte->file, spte->file_offset);
          int read_bytes = file_read (spte->file, kernel_virtual_page_in_user_pool, spte->read_bytes);
          ASSERT(read_bytes == spte->read_bytes);
          ASSERT (spte->read_bytes + spte->zero_bytes == PGSIZE);
          memset (kernel_virtual_page_in_user_pool + read_bytes, 0, spte->zero_bytes);
          ASSERT(reinstall_page(spte->user_page, kernel_virtual_page_in_user_pool, spte->writable));
          // vm_load_IN_FILE_to_user_pool(spte) over

          newly_allocated = true;
        }


        /* 이번 user_pointer_inclusive +i가 나타내는 frame을 구하고 user pointer를 위해 쓰인다고 기록한다. */
        //same as vm_frame_lookup_same_keys(spte->kernel_virtual_page_in_user_pool); 
        struct frame_table_entry key;
        key.kernel_virtual_page_in_user_pool = spte->kernel_virtual_page_in_user_pool;
        struct vm_ft_same_keys* founds = vm_ft_hash_find_same_keys(&frame_table, &(key.elem));

        vm_frame_set_for_user_pointer(founds, true);
        vm_ft_same_keys_free(founds);

        // same as vm_frame_setting_over(vm_frame_lookup_same_keys(spte->kernel_virtual_page_in_user_pool));
        key.kernel_virtual_page_in_user_pool = spte->kernel_virtual_page_in_user_pool;
        founds = vm_ft_hash_find_same_keys(&frame_table, &(key.elem));
        for(int i = 0; i < founds->len; ++i){
          struct frame_table_entry* fte = vm_ft_hash_entry(founds->pointers_arr_of_ft_hash_elem[i], struct frame_table_entry, elem);
          fte->setting_now = false;
        }
        vm_ft_same_keys_free(founds);

sema_up(&frame_table_w);
      }
  }
}
void unmake(void* user_pointer_inclusive, size_t bytes){
  struct thread* t = thread_current();

  void* new_page = NULL;
  for(size_t i = 0; i < bytes; ++i){
      //페이지가 달라진 경우
      if(new_page != pg_round_down(user_pointer_inclusive + i)){
        //advanced
        new_page = pg_round_down(user_pointer_inclusive + i);

        //이번 user_pointer_inclusive + i가 속하는 페이지의 spte를 구한다.
        struct supplemental_page_table_entry* spte = vm_spt_lookup(&t->spt, new_page);

        //이번 user_pointer_inclusive +i가 나타내는 frame을 구하고 user pointer를 위해 쓰인다고 기록한다.
        struct vm_ft_same_keys* founds = vm_frame_lookup_same_keys(spte->kernel_virtual_page_in_user_pool); 
        vm_frame_set_for_user_pointer(founds, false);
        vm_ft_same_keys_free(founds);
      }
  }
}





/* hash function들 */
static unsigned frame_table_hash_func(const struct vm_ft_hash_elem *e, void* aux UNUSED)
{
  struct frame_table_entry* fte = vm_ft_hash_entry(e, struct frame_table_entry, elem);
  return vm_ft_hash_int((int)fte->kernel_virtual_page_in_user_pool);
}
static bool frame_table_less_func(const struct vm_ft_hash_elem *a, const struct vm_ft_hash_elem *b, void *aux UNUSED)
{
  struct frame_table_entry *fte_a = vm_ft_hash_entry(a, struct frame_table_entry, elem);
  struct frame_table_entry *fte_b = vm_ft_hash_entry(b, struct frame_table_entry, elem);
  return fte_a->kernel_virtual_page_in_user_pool < fte_b->kernel_virtual_page_in_user_pool;
}
static bool frame_table_value_less_func(const struct vm_ft_hash_elem*a, const struct vm_ft_hash_elem*b, void *aux)
{
  struct frame_table_entry *fte_a = vm_ft_hash_entry(a, struct frame_table_entry, elem);
  struct frame_table_entry *fte_b = vm_ft_hash_entry(b, struct frame_table_entry, elem);
  return fte_a->user_page < fte_b->user_page;
}