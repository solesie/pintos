#include <bitmap.h>
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/swap.h"
#include "threads/synch.h"
#include "filesys/off_t.h"
#include "threads/palloc.h"
#include "vm/page.h"

/* block_read(), block_write() wrapper and bitmap manipulation functions are defined in swap.c */

/* 한 섹터의 크기는 512bytes, 한 페이지(프레임)의 크기는 4096bytes이다.
   8개의 연속된 섹터가 한 frame을 나타낸다.
   4.1.2) swap slots should be page-aligned because there is no downside in doing so. */
#define NUM_OF_SECTORS_ON_A_FRAME (PGSIZE / BLOCK_SECTOR_SIZE)

/* Partition that contains the swap system. */
struct block* swap_device;

/* swap table bitmap 대신 배열을 사용한다. 값은 현재 이 swap slot을 사용하는 프로세스의 개수를 나타낸다. */
static uint8_t* swap_table;
static int swap_table_len;

/* bitmap 조작만은 무조건 lock이 걸려져야 한다. */
static struct lock swap_table_mutex;

/* Initializes the swap system module. */
void vm_swapsys_init(){
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("No swap system device found, can't initialize swap system.");

  /* free map init */
  swap_table_len = (block_size (swap_device) / NUM_OF_SECTORS_ON_A_FRAME);
  swap_table = (uint8_t*)malloc(sizeof(uint8_t) * swap_table_len);
  memset(swap_table, 0, sizeof(uint8_t) * swap_table_len);

  if (swap_table == NULL)
    PANIC ("swap_table creation failed--swap system device is too large");
  lock_init(&swap_table_mutex);
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
  lock_acquire(&swap_table_mutex);
  /* kernel space에 존재하므로 free_map->bits[swap_slot] available for use. */
  swap_table[swap_slot] = 0;
  lock_release(&swap_table_mutex);
}

/* kernel_virtual_page_in_user_pool를 swap_device에 기록한다.
   페이지를 기록한 swap slot의 번호를 반환한다.
   write(swap_slot, kernel_virtual_page_in_user_pool, sizeof(PGSIZE)) 느낌 */
size_t vm_swap_out(void* kernel_virtual_page_in_user_pool, int sharing_proc_num){
  lock_acquire(&swap_table_mutex);
  size_t swap_slot;
  for(swap_slot = 0; swap_slot < swap_table_len; ++swap_slot)
    if(swap_table[swap_slot] == 0){
      swap_table[swap_slot] = sharing_proc_num;
      break;
    }
  lock_release(&swap_table_mutex);

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


void
vm_swap_free (size_t swap_slot)
{
  lock_acquire(&swap_table_mutex);
  ASSERT(swap_table[swap_slot] >=0);
  --swap_table[swap_slot];
  lock_release(&swap_table_mutex);
}