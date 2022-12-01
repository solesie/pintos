#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/synch.h"
#include "threads/palloc.h"

void vm_frame_init (void);
void* vm_frame_allocate (enum palloc_flags, void *);

struct frame_table_entry;
void vm_frame_free (struct frame_table_entry* fte);
void vm_frame_free_only_in_ft(struct frame_table_entry* fte);

struct frame_table_entry* vm_frame_lookup(void* kernel_virtual_page_in_user_pool);
void vm_frame_set_for_user_pointer(struct frame_table_entry* fte, bool value);

#endif /* vm/frame.h */