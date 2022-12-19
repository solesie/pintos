#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"

void buffer_cache_init (void);
void buffer_cache_terminate (void);

void buffer_cache_read (block_sector_t sector, void *buffer, int sector_ofs, int chunk_size);

void buffer_cache_write (block_sector_t sector, void *buffer, int sector_ofs, int chunk_size);

#endif