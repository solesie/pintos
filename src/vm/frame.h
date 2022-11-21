#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/synch.h"
#include "threads/palloc.h"

void vm_frame_init (void);
void* vm_frame_allocate (enum palloc_flags, void *);
void vm_frame_free (void*);

#endif /* vm/frame.h */