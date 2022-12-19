#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include <string.h>
#include <debug.h>
#include "threads/vaddr.h"

struct buffer_cache_entry {
  bool valid_bit;                    // true if valid cache entry
  bool reference_bit;                // for clock algorithm
  bool dirty;

  block_sector_t disk_sector;
  uint8_t buffer[BLOCK_SECTOR_SIZE]; // 512 * 1B
};

#define NUM_CACHE 64

/* Buffer cache */
static struct buffer_cache_entry cache[NUM_CACHE];

/* 모두 writers 함수로 봐도 무방 */
struct lock buffer_cache_lock;

static struct buffer_cache_entry* buffer_cache_lookup (block_sector_t sector);
static void buffer_cache_flush_entry(struct buffer_cache_entry* entry);
static struct buffer_cache_entry* buffer_cache_select_victim ();
static struct buffer_cache_entry* buffer_cache_allocate();
static void buffer_cache_flush_all();


void buffer_cache_init (void){
  lock_init (&buffer_cache_lock);

  for (int i = 0; i < NUM_CACHE; ++ i)
    cache[i].valid_bit = false;
}

void buffer_cache_terminate (void){
  lock_acquire(&buffer_cache_lock);

  buffer_cache_flush_all();

  lock_release(&buffer_cache_lock);
}


/* sector에 있는 값을 buffer로 읽어들인다.
   @param sector_ofs: sector에 있는 값을 읽어들일때 시작점
   @param chunk_size: 실제로 이 sector에서 읽어들일 bytes */
void buffer_cache_read (block_sector_t sector, void *buffer, int sector_ofs, int chunk_size){
  lock_acquire(&buffer_cache_lock);
  struct buffer_cache_entry* slot = buffer_cache_lookup(sector);
  if(slot == NULL){
    slot = buffer_cache_allocate();
    slot->valid_bit = true;
    slot->dirty = false;
    slot->disk_sector = sector;
    block_read(fs_device, sector, slot->buffer);
  }

  slot->reference_bit = true;
  memcpy(buffer, slot->buffer + sector_ofs, chunk_size);

  lock_release(&buffer_cache_lock);
}


/* buffer에 있는 값을 실제로 sector에 적는게 아닌 buffer_cache에 적는다.
   @param sector_ofs: sector에 적는 시작점
   @param chunk_size: 실제로 이 sector에 적을 bytes  */
void buffer_cache_write (block_sector_t sector, void *buffer, int sector_ofs, int chunk_size){
  lock_acquire(&buffer_cache_lock);
  struct buffer_cache_entry* slot = buffer_cache_lookup(sector);
  
  if(slot == NULL){
    slot = buffer_cache_allocate();
    slot->valid_bit = true;
    slot->dirty = false;
    slot->disk_sector = sector;
    block_read(fs_device, sector, slot->buffer);
  }

  slot->reference_bit = true;
  slot->dirty = true;
  memcpy(slot->buffer + sector_ofs, buffer, chunk_size);

  lock_release(&buffer_cache_lock);
}


/* buffer cache 중에 sector와 일치하는 entry를 반환한다.
   없다면 null을 반환한다. */
static struct buffer_cache_entry* buffer_cache_lookup (block_sector_t sector){
  for(int i = 0; i < NUM_CACHE; ++i){
    if(cache[i].valid_bit && cache[i].disk_sector == sector)
      return &(cache[i]);
  }
  return NULL;
}


/* clock algorithm 사용 */
static struct buffer_cache_entry* buffer_cache_select_victim (){
  static size_t clock = 0;
  while (true) {
    ASSERT(cache[clock].valid_bit == true)

    if (cache[clock].reference_bit) {
      // second chance
      cache[clock].reference_bit = false;
    }
    else
      return &(cache[clock]);

    clock ++;
    clock %= NUM_CACHE;
  }
}


/* 빈 슬롯이 없다면 evict 해서라도 빈 슬롯을 반환한다. */
static struct buffer_cache_entry* buffer_cache_allocate(){
  // 빈 슬롯이 있는 경우
  for(int i = 0; i < NUM_CACHE; ++i){
    if(!cache[i].valid_bit)
      return &(cache[i]);
  }
  // 빈 슬롯이 없는 경우
  struct buffer_cache_entry* empty = buffer_cache_select_victim();
  if (empty->dirty) {
    // write back into disk
    buffer_cache_flush_entry(empty);
  }

  empty->valid_bit = false;
  return empty;
}


static void buffer_cache_flush_entry(struct buffer_cache_entry* entry){
  ASSERT(entry->valid_bit == true && entry->dirty == true);

  block_write(fs_device, entry->disk_sector, entry->buffer);
  entry->dirty = false;
}


static void buffer_cache_flush_all(){
  for(int i = 0; i < NUM_CACHE; ++i){
    if(cache[i].valid_bit && cache[i].dirty){
      buffer_cache_flush_entry(&(cache[i]));
      cache[i].dirty = false;
    }
  }
}