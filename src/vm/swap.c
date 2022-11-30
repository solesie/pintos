#include <bitmap.h>
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/swap.h"
#include "threads/synch.h"
#include "filesys/off_t.h"
#include "threads/palloc.h"
#include "vm/page.h"

/* 한 섹터의 크기는 512bytes, 한 페이지(프레임)의 크기는 4096bytes이다.
   8개의 연속된 섹터가 한 frame을 나타낸다.
   4.1.2) swap slots should be page-aligned because there is no downside in doing so. */
#define NUM_OF_SECTORS_ON_A_FRAME (PGSIZE / BLOCK_SECTOR_SIZE)

/* Partition that contains the swap system. */
struct block* swap_device;

static struct bitmap *free_map;      /* Free map, one bit per sector. */

/* bitmap 조작만은 무조건 lock이 걸려져야 한다. */
static struct lock bitmap_lock;

/* Initializes the swap system module. */
void vm_swapsys_init(){
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("No swap system device found, can't initialize swap system.");

  /* free map init */
  free_map = bitmap_create (block_size (swap_device) / NUM_OF_SECTORS_ON_A_FRAME); //free-map 생성 후 false로 초기화
  if (free_map == NULL)
    PANIC ("bitmap creation failed--swap system device is too large");
  lock_init(&bitmap_lock);
}

/* swap_device에서 swap_slot에 저장된 데이터를 kernel_virtual_page_in_user_pool에 복사한다.
   read(swap_slot, kernel_virtual_page_in_user_pool, sizeof(PGSIZE)) 느낌 */
void vm_swap_in(size_t swap_slot, void* kernel_virtual_page_in_user_pool){
  block_sector_t start = (block_sector_t)swap_slot * NUM_OF_SECTORS_ON_A_FRAME;
  /* start부터 한 프레임 분량의 섹터(NUM_OF_SECTORS_ON_A_FRAME)를 읽어서 
     kernel_virtual_page_in_user_pool에다 기록한다.  */
  off_t bytes_read;
  block_sector_t sector_idx;

  for(bytes_read = 0, sector_idx = start; sector_idx < start + NUM_OF_SECTORS_ON_A_FRAME;
        ++sector_idx, bytes_read += BLOCK_SECTOR_SIZE){
    /* Read full one sector directly into kernel_virtual_page_in_user_pool. */
    block_read (swap_device, sector_idx, kernel_virtual_page_in_user_pool + bytes_read);
  }

  /* kernel space에 존재하므로 free_map->bits[swap_slot] available for use. */
  bitmap_set(free_map, swap_slot, false);
}

/* kernel_virtual_page_in_user_pool를 swap_device에 기록한다.
   페이지를 기록한 swap slot의 번호를 반환한다.
   write(swap_slot, kernel_virtual_page_in_user_pool, sizeof(PGSIZE)) 느낌 */
size_t vm_swap_out(void* kernel_virtual_page_in_user_pool){
  lock_acquire(&bitmap_lock);
  size_t swap_slot = bitmap_scan_and_flip (free_map, 0, 1, false);
  lock_release(&bitmap_lock);

  block_sector_t start = (block_sector_t)swap_slot * NUM_OF_SECTORS_ON_A_FRAME;
  off_t bytes_read;
  block_sector_t sector_idx;
  
  for(bytes_read = 0, sector_idx = start; sector_idx < start + NUM_OF_SECTORS_ON_A_FRAME;
        ++sector_idx, bytes_read += BLOCK_SECTOR_SIZE){
    /* Write full one sector directly into kernel_virtual_page_in_user_pool. */
    block_write (swap_device, sector_idx, kernel_virtual_page_in_user_pool + bytes_read);
  }
  return swap_slot;
}


/* swap device의 데이터를 spte->kernel_virtual_page_in_user_pool에 올려둔다.
   성공 여부를 반환한다. proj4 pptx)Page Fault Handler 참조 */
bool vm_load_SWAP_to_user_pool(struct supplemental_page_table_entry* spte){
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
    vm_frame_free(kernel_virtual_page_in_user_pool);
    return false;
  }
  
  return true;
}