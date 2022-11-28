#include <hash.h>
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"

static unsigned spte_hash_func(const struct hash_elem *elem, void *aux);
static bool spte_less_func(const struct hash_elem *, const struct hash_elem *, void *aux);

void vm_spt_create(struct hash* spt){
    hash_init(spt, spte_hash_func, spte_less_func, NULL);
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

static unsigned spte_hash_func(const struct hash_elem *elem, void *aux UNUSED){
  struct supplemental_page_table_entry *spte = hash_entry(elem, struct supplemental_page_table_entry, elem);
  return hash_int( (int)spte->user_page );
}
static bool spte_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  struct supplemental_page_table_entry *spte_a = hash_entry(a, struct supplemental_page_table_entry, elem);
  struct supplemental_page_table_entry *spte_b = hash_entry(b, struct supplemental_page_table_entry, elem);
  return spte_a->user_page < spte_b->user_page;
}