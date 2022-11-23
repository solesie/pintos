#include <hash.h>
#include "vm/page.h"
#include "threads/vaddr.h"

static unsigned spte_hash_func(const struct hash_elem *elem, void *aux);
static bool spte_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);

void vm_spt_create(struct hash* supplemental_page_table){
    hash_init(supplemental_page_table, spte_hash_func, spte_less_func, NULL);
}

/* spt에   */
bool vm_spt_set_page(struct hash* supplemental_page_table, void* user_page, void* kernel_virtual_page){
  
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