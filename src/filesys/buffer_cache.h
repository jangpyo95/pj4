#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include <hash.h>
#include <list.h>

//this whole file is  added4 

struct buffer_head {
        bool dirty;     //dirty flag
        bool being_used; //being used flag
        bool access;    //access flag (whether accessed recently)
        block_sector_t sector; //on-disk location 
        //size_t index; 
        struct hash_elem he;
        struct list_elem le;
        uint8_t data[512];//just added data under the header cuz separate buffercache complicated the whole design
};


void buffer_cache_init(void);
struct buffer_head * cache_lookup(block_sector_t sector);
void cache_read(block_sector_t sector, void * buffer, int ofs, int chunk_size);
void cache_write(block_sector_t sector, void * buffer, int ofs, int chunk_size);
struct buffer_head * cache_evict(void);

#endif /* filesys/buffer_cache.h */
