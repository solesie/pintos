#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/frame-table-hash.h"
#include "vm/page.h"

void vm_frame_init (void);
void* vm_frame_allocate (enum palloc_flags, void *);

void make_user_pointer_in_physical_memory(void* user_pointer_inclusive, size_t bytes);
void unmake(void* user_pointer_inclusive, size_t bytes);

struct frame_table_entry;
void vm_frame_free (struct frame_table_entry* fte);
void vm_frame_free_only_in_ft(struct frame_table_entry* fte);

struct frame_table_entry* vm_frame_lookup_exactly_identical(struct supplemental_page_table_entry* spte);
struct vm_ft_same_keys* vm_frame_lookup_same_keys(void* kernel_virtual_page_in_user_pool);

void vm_frame_setting_over(struct vm_ft_same_keys* founds);

#endif /* vm/frame.h */