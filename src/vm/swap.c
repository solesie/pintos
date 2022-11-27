#include <bitmap.h>
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/swap.h"
#include "threads/synch.h"

/* 한 섹터의 크기는 512bytes, 한 페이지의 크기는 4096bytes이다.
   8개의 연속된 섹터가 한 페이지를 나타낸다.
   4.1.2) swap slots should be page-aligned because there is no downside in doing so. */
#define NUM_OF_SECTORS_ON_A_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Partition that contains the swap system. */
struct block* swap_device;

static struct bitmap *free_map;      /* Free map, one bit per sector. */

/* bitmap 조작만은 무조건 lock이 걸려져야 한다. */
static struct lock bitmap_lock;

/* Initializes the swap system module. */
void vm_swapsys_init(){
  swap_device = block_get_role (BLOCK_FILESYS);
  if (swap_device == NULL)
    PANIC ("No swap system device found, can't initialize swap system.");

  /* free map init */
  free_map = bitmap_create (block_size (swap_device)/NUM_OF_SECTORS_ON_A_PAGE); //페이지 개수만큼 free-map 생성 후 false로 초기화
  if (free_map == NULL)
    PANIC ("bitmap creation failed--swap system device is too large");
  lock_init(&bitmap_lock);
}
