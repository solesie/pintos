#ifndef VM_SWAP_H
#define VM_SWAP_H

void vm_swapsys_init();
void vm_swap_in(size_t, void* );
size_t vm_swap_out(void* ,int);

#endif