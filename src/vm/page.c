#include <hash.h>
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"

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
  spte->frame_data_clue = SWAP;
  spte->kernel_virtual_page_in_user_pool = NULL;
  spte->swap_slot = swap_slot;
}


/* swap device의 데이터를 frame table에 올려둔다(swap.c:vm_swap_in() 사용).
   성공 여부를 반환한다. proj4 pptx)Page Fault Handler 참조 */
bool vm_load_spte_to_user_pool(struct supplemental_page_table_entry* spte){
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
    struct frame_table_entry* fte = vm_frame_lookup_exactly_identical(kernel_virtual_page_in_user_pool);
    vm_frame_free_only_in_ft(fte);
    struct vm_ft_same_keys* others = vm_frame_lookup_same_keys(kernel_virtual_page_in_user_pool);
    if(others == NULL){ //no sharing
      palloc_free_page(kernel_virtual_page_in_user_pool);
    }
    if(others != NULL) //sharing(do nothing)
      vm_ft_same_keys_free(others);
    return false;
  }
  
  return true;
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
  else if(entry->frame_data_clue == SWAP) {
    vm_swap_free (entry->swap_slot);
  }

  // Clean up SPTE entry.
  free (entry);
}