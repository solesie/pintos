#include <hash.h>
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "lib/string.h"
#include "vm/swap.h"

static unsigned spte_hash_func(const struct hash_elem *elem, void *aux);
static bool spte_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);
static void spte_destroy_func(struct hash_elem *elem, void *aux UNUSED);

void vm_spt_create(struct hash* spt){
  hash_init(spt, spte_hash_func, spte_less_func, NULL);
}


void vm_spt_destroy (struct hash* spt){
  hash_destroy (spt, spte_destroy_func);
}


/* spt에서 user_page를 key로 spte를 찾는다.
   있으면 spte, 없다면 NULL 반환. */
struct supplemental_page_table_entry* vm_spt_lookup(struct hash* spt, void* user_page){
  struct supplemental_page_table_entry key;
  key.user_page = user_page;
  struct hash_elem* elem = hash_find(spt, &key.elem);
  if(elem == NULL)
    return NULL;
  return hash_entry(elem, struct supplemental_page_table_entry, elem);
}


void vm_spt_update_after_swap_out(struct supplemental_page_table_entry* spte, size_t swap_slot){
  spte->frame_data_clue = IN_SWAP;
  spte->kernel_virtual_page_in_user_pool = NULL;
  spte->swap_slot = swap_slot;
}


/* swap device의 데이터를 frame table에 올려둔다(swap.c:vm_swap_in() 사용).
   성공 여부를 반환한다. proj4 pptx)Page Fault Handler 참조 */
bool vm_load_IN_SWAP_to_user_pool(struct supplemental_page_table_entry* spte){
  ASSERT(spte->frame_data_clue == IN_SWAP);
  //Is there remaining?
  void* kernel_virtual_page_in_user_pool = vm_frame_allocate(PAL_USER, spte -> user_page);//Page replacement algorithm
  if(kernel_virtual_page_in_user_pool == NULL){
    PANIC("frame allocate 에러");
    return false;
  }

  //Swap page into frame from disk
  vm_swap_in(spte->swap_slot, kernel_virtual_page_in_user_pool);

  //Modify page and swap manage tables
  if(!reinstall_page(spte->user_page, kernel_virtual_page_in_user_pool, spte->writable)) {
    PANIC("install_page 에러");
    struct supplemental_page_table_entry key;
    key.kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;
    key.user_page = spte->user_page;
    struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(&key);
    vm_frame_free(fte);
    return false;
  }
  
  return true;
}


bool vm_load_IN_FILE_to_user_pool(struct supplemental_page_table_entry* spte){
  ASSERT(spte->frame_data_clue == IN_FILE);
  //Is there remaining?
  void* kernel_virtual_page_in_user_pool = vm_frame_allocate(PAL_USER, spte -> user_page);//Page replacement algorithm
  if(kernel_virtual_page_in_user_pool == NULL){
    PANIC("frame allocate 에러");
    return false;
  }

  file_seek(spte->file, spte->file_offset);
  
  int read_bytes = file_read (spte->file, kernel_virtual_page_in_user_pool, spte->read_bytes);
  if(read_bytes != (int)spte->read_bytes){
    PANIC("file_read 에러");
    struct supplemental_page_table_entry key;
    key.kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;
    key.user_page = spte->user_page;
    struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(&key);
    vm_frame_free(fte);
    return false;
  }

  ASSERT (spte->read_bytes + spte->zero_bytes == PGSIZE);
  memset (kernel_virtual_page_in_user_pool + read_bytes, 0, spte->zero_bytes);

  if(!reinstall_page(spte->user_page, kernel_virtual_page_in_user_pool, spte->writable)) {
    PANIC("install_page 에러");
    struct supplemental_page_table_entry key;
    key.kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;
    key.user_page = spte->user_page;
    struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(&key);
    vm_frame_free(fte);
    return false;
  }

  return true;
}


/* 기존에 프로세스 종료시 메모리에서 프로세스의 데이터를 없애기만 하면 되는것과는 다르게
   mmap으로 할당한 file-backed page는 변경되었을 시에는 disk에 기록되어야한다.
   dirty bit로 판단 가능하다. */
bool vm_save_IN_FRAME_to_file(struct thread* t, struct supplemental_page_table_entry* spte){
  ASSERT(spte->frame_data_clue == IN_FRAME);
  ASSERT(spte->file != NULL);
  bool dirty = pagedir_is_dirty(t->pagedir, spte->user_page) || pagedir_is_dirty(t->pagedir, spte->kernel_virtual_page_in_user_pool);

  if(dirty){
    file_write_at(spte->file, spte->user_page, spte->read_bytes,spte->file_offset);
  }
  struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(spte);
  vm_frame_free(fte);
  pagedir_clear_page(t->pagedir, spte->user_page);
  hash_delete(&t->spt, &spte->elem);
  free(spte);
}




/* 이미 spt에 user_page가 존재하는 경우는 kernel_virtual_page_in_user_pool로 갱신한다. */
void vm_spt_set_IN_FRAME_page(struct hash* spt, void* user_page, void* kernel_virtual_page_in_user_pool
, bool writable){
  struct supplemental_page_table_entry* spte = vm_spt_lookup(spt, user_page);
  ASSERT(spte != NULL);

  spte->kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;
  spte->frame_data_clue = IN_FRAME;
  spte->writable = writable;
  return;
}


/* spt에 user_page와 kernel_virtual_page_in_user_pool를 연관시킨 spte를 삽입한다. */
void vm_spt_install_IN_FRAME_page(struct hash* spt, void* user_page, void* kernel_virtual_page_in_user_pool
, bool writable){
  struct supplemental_page_table_entry* spte = vm_spt_lookup(spt, user_page);
  ASSERT(spte == NULL);

  /* spt에 아예 존재하지 않는경우는 새로 설치한다. */
  spte = (struct supplemental_page_table_entry*) malloc(sizeof(struct supplemental_page_table_entry));
  spte->user_page = user_page;
  spte->frame_data_clue = IN_FRAME;
  spte->kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;
  spte->writable = writable;

  hash_insert (spt, &spte->elem);
  return;
}


/* spt에 user_page를 나타내는 spte를 삽입한다.
   lazy load를 위해 사용된다. */
void vm_spt_install_IN_FILE_page (struct hash *spt, void *user_page,
    struct file * file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes, bool writable){
  struct supplemental_page_table_entry *spte 
  = (struct supplemental_page_table_entry *) malloc(sizeof(struct supplemental_page_table_entry));

  spte->user_page = user_page;
  spte->kernel_virtual_page_in_user_pool = NULL;
  spte->frame_data_clue = IN_FILE;
  spte->writable = writable;
  spte->file = file;
  spte->file_offset = offset;
  spte->read_bytes = read_bytes;
  spte->zero_bytes = zero_bytes;

  hash_insert (spt, &spte->elem);
}



static unsigned spte_hash_func(const struct hash_elem *elem, void *aux UNUSED){
  struct supplemental_page_table_entry *spte = hash_entry(elem, struct supplemental_page_table_entry, elem);
  return hash_int( (int)spte->user_page );
}
static bool spte_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  struct supplemental_page_table_entry *spte_a = hash_entry(a, struct supplemental_page_table_entry, elem);
  struct supplemental_page_table_entry *spte_b = hash_entry(b, struct supplemental_page_table_entry, elem);
  return spte_a->user_page < spte_b->user_page;
}
static void spte_destroy_func(struct hash_elem *elem, void *aux UNUSED){
  struct supplemental_page_table_entry *entry = hash_entry(elem, struct supplemental_page_table_entry, elem);

  /* user program이 종료될 때 pagedir_destory()가 호출되면서 실제 physical memory에서 free된다. 
     이때 frame_table에서 지워주어야한다. */
  if (entry->frame_data_clue == IN_FRAME) {
    vm_frame_free_only_in_ft(vm_frame_lookup_exactly_identical(entry));
  }
  else if(entry->frame_data_clue == IN_SWAP) {
    vm_swap_free (entry->swap_slot);
  }

  // Clean up SPTE entry.
  free (entry);
}