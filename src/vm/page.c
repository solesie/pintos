#include <hash.h>
#include "vm/page.h"
#include "threads/vaddr.h"

static unsigned spte_hash_func(const struct hash_elem *elem, void *aux);
static bool spte_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);

void vm_spt_create(struct hash* spt){
    hash_init(spt, spte_hash_func, spte_less_func, NULL);
}

/* spt에 user_page와 
   physical_memory 상에 존재하는 kernel_virtual_page_in_user_pool를 
   연관시킨 spte를 삽입한다. */
bool vm_spt_set_IN_FRAME_page(struct hash* spt, void* user_page, void* kernel_virtual_page_in_user_pool){
  struct supplemental_page_table_entry* spte 
  = (struct supplemental_page_table_entry*) malloc(sizeof(struct supplemental_page_table_entry));

  if(spte == NULL)
    return false;

  spte->user_page = user_page;
  spte->frame_data_clue = IN_FRAME;
  spte->kernel_virtual_page_in_user_pool = kernel_virtual_page_in_user_pool;

  if (hash_insert (spt, &spte->elem) == NULL) {
    return true;
  }
  // 이미 spte가 존재하는 경우
  free (spte);
  return false;
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