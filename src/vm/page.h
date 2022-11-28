#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/swap.h"
#include "filesys/off_t.h"

enum clue_of_frame_data{
    SWAP,           /* swap disk에 존재한다. */
    IN_FRAME        /* 현재 physical memory에 존재한다. */
    //ZEROING         /* mmap에서 이용(구현 사항 없음) */
};
/* supplemental page table은 추가적인 정보로 page table을 보완한다.
   즉, page table을 나타내고, per process이다.
   supplemental page table entry를 빠르게 탐색할 수 있도록 해시테이블을 사용한다.
   (key, value) = (user_page, supplemental_page_table_entry)

   pte에 dirty bit, present bit... 같은것도 존재하는데 supplemental page table이 필요한 이유
   = fault가 일어난 virtual page가 가르키는 frame에 대체 어떤 내용이 있어야 하는지 어떻게 알 것인가? 
   (이 내용은 디스크의 어느어느 위치에 저장되어 있다던지, 이 내용은 어느 파일을 어디어디부터 다시 읽으면 되는지...)
   virtual page가 나타내는 logical address로 찾아간 pte에선 dirty bit, present bit... 이런건 알지만
   정확히 어떤 내용이 있었는가의 정보는 얻을 수 없는 상태이다.

     31                                   12 11 9      6 5     2 1 0
    +---------------------------------------+----+----+-+-+---+-+-+-+
    |           Physical Address            | AVL|    |D|A|   |U|W|P|
    +---------------------------------------+----+----+-+-+---+-+-+-+
                            <page table entry>
    (pte자체가 supplemental page table을 추적해도 되지만, 이는 advanced students only) */
struct supplemental_page_table_entry{
    void *user_page;
    void *kernel_virtual_page_in_user_pool;           /* If the page is not on the frame, should be NULL. */
    
    struct hash_elem elem;

    enum clue_of_frame_data frame_data_clue;         /* frame에 대체 어떤 내용이 있어야 하는지 어떻게 알 것인가? */

    bool writable;                                   /* same as pte R/W bit */

    size_t swap_slot;                                /* frame이 swap_device에 존재하는 경우 어느 슬롯에 잇는가 */
};

void vm_spt_create(struct hash*);
struct supplemental_page_table_entry* vm_spt_lookup(struct hash*, void*);

#endif /* vm/page.h */