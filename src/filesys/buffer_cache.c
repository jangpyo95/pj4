#include "filesys/buffer_cache.h"
#include <bitmap.h>
#include <hash.h>
#include <string.h>
#include "filesys/filesys.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "devices/timer.h"

//added 4 this whole file is added in lab 4
#define NUM_BLOCKS 64
 
//static void *cache_base_addr; //base address of the cache 
//static struct buffer_head buffer_heads[64]; //array of buffer heads
static struct lock cache_lock; //cache lock
static struct hash cache_hash; //cache hashmap
static struct list cache_list; //buffer head list

/*
struct buffer_head {
	bool dirty;	//dirty flag
	bool being_used; //being used flag
	bool access; 	//access flag (whether accessed recently)
	block_sector_t sector; //on-disk location 
	//size_t index; 
	struct hash_elem he; 	
	struct list_elem le;
	uint8_t data[512];//just added data under the header cuz separate buffercache complicated the whole design
};
*/
static unsigned cache_hash_func(const struct hash_elem *e , void *aux UNUSED){

  struct buffer_head *bh;

  bh = hash_entry(e,struct buffer_head,he);

  return hash_int((int)bh->sector);
}

static bool cache_less_func(struct hash_elem *A , struct hash_elem *B , void *aux UNUSED){

  struct buffer_head *bh_A = hash_entry(A,struct buffer_head,he);
  struct buffer_head *bh_B = hash_entry(B,struct buffer_head,he);
  return bh_A->sector < bh_B->sector;
}


void buffer_cache_init (){
//	cache_base_addr=palloc_get_multiple(PAL_ASSERT,8); //1 pg = 4096 bytes = 8 blocks. get 8pgs
//	buffer_heads=(struct buffer_head *) malloc(sizeof (struct buffer_head)*64);
	lock_init(&cache_lock);
	hash_init(&cache_hash, (hash_hash_func *) &cache_hash_func, (hash_less_func *) &cache_less_func, NULL);
	list_init(&cache_list);
}

//find cache entry with sector no. If not found, return NULL
struct buffer_head * cache_lookup(block_sector_t sector){

  struct buffer_head bh;
  bh.sector = sector;
  
  lock_acquire(&cache_lock);
 
  struct hash_elem *h_elem = hash_find(&cache_hash,&bh.he);

  lock_release(&cache_lock);

  if(h_elem == NULL){
	return NULL;
  }

  return hash_entry(h_elem,struct buffer_head , he);
}

/*
Read with buffer cache 
1. find buffer head
2. if exist, read data from buffer cache to buffer&update buffer_head
3. if not exist, check cache is full
4. If full, select victim entry & flush if necessary
5. If not full,select empty entry & read data from disk to cache
6. Then read data from buffer cache to buffer
*/

void cache_read(block_sector_t sector, void * buffer, int ofs, int chunk_size){
	struct buffer_head *bh = cache_lookup(sector);
	//If Read misses(no match)
	if(bh==NULL){
		//cache has empty space
		if(list_size(&cache_list) < NUM_BLOCKS){
			bh=(struct buffer_head*)malloc(sizeof(struct buffer_head));
			bh->access=true;
			bh->dirty = false;
			bh->being_used = true;
			bh->sector = sector;
			//reading the whole block
			if(ofs==0 && chunk_size == 512){
				block_read(fs_device, sector, bh->data); //read from disk to cache(whole block)
				memcpy(buffer,bh->data, chunk_size);//copy to buffer
			} else { //reading partial
				block_read(fs_device, sector, bh->data);
				memset(bh->data, 0, ofs);
				memcpy(buffer,bh->data+ofs, chunk_size);
			}

			//update buffer_head hashmap & add to cache list
			lock_acquire(&cache_lock);
			hash_insert(&cache_hash,&bh->he);
			list_push_back(&cache_list,&bh->le);
			lock_release(&cache_lock);
		} else{ //cache full, so evict one
			bh = cache_evict();
			bh->sector = sector;
			 bh->access=true;
                        bh->dirty = false;
                        bh->being_used = true;

			//reading the whole block
                        if(ofs==0 && chunk_size == 512){
                                block_read(fs_device, sector, bh->data); //read from disk to cache(whole block)
                                memcpy(buffer,bh->data, chunk_size);//copy to buffer
                        } else { //reading partial
                                block_read(fs_device, sector, bh->data);
                                memset(bh->data, 0, ofs);
                                memcpy(buffer,bh->data+ofs, chunk_size);
                        }
                
                        //update buffer_head hashmap & add to cache list
                        lock_acquire(&cache_lock);
                        hash_insert(&cache_hash,&bh->he);
                        lock_release(&cache_lock);
			}
		} else{//cache hit(existing already)
			if(bh->being_used == true){
				bh->access=true;
				memcpy(buffer,bh->data+ofs,chunk_size);
			} else {
				PANIC("Cache Read Error! Existing Cache invalid");
			}
		}
	}



/*
Write with buffer cache 
1. find buffer head
2. if exist, write buffer's data to cache & update header
3. if not exist, check cache is full
4. If full, select victim entry & flush if necessary
5. If not full,select empty entry & read data from disk to cache
6. Then write buffer's data to buffer cache
*/
void cache_write(block_sector_t sector, void * buffer, int ofs, int chunk_size){
	struct buffer_head *bh = cache_lookup(sector);
	//If Write misses(no match)
	if(bh==NULL){
		//cache has empty space
		if(list_size(&cache_list) < NUM_BLOCKS){
			bh=(struct buffer_head*)malloc(sizeof(struct buffer_head));
			bh->access=true;
			bh->dirty = false;
			bh->being_used = true;
			bh->sector = sector;
			//writing the whole block
			if(ofs==0 && chunk_size == 512){
				block_write(fs_device, sector,buffer); //write whole block
				memcpy(bh->data,buffer, chunk_size);//copy to cache
			} else { //writing partial
				int sector_length = 512 - ofs; //left over length
				if(ofs > 0 || chunk_size < sector_length){
					//read full block(only from offset will be used)
					block_read(fs_device, sector, bh->data);
				} else {
					memset(bh->data, 0, 512);
				}
				memcpy(bh->data+ofs, buffer, chunk_size); //update cache
				block_write(fs_device, sector, bh->data);//update disk block
			}

			//update buffer_head hashmap & add to cache list
			lock_acquire(&cache_lock);
			hash_insert(&cache_hash,&bh->he);
			list_push_back(&cache_list,&bh->le);
			lock_release(&cache_lock);
		} else{ //cache full, so evict one
			bh = cache_evict();
			bh->sector = sector;
			 bh->access=true;
                        bh->dirty = false;
                        bh->being_used = true;
						
			//writing the whole block
                        if(ofs==0 && chunk_size == 512){                               
				block_write(fs_device, sector,buffer); //write whole block
                                memcpy(bh->data,buffer, chunk_size);//copy to cache
                        } else { //writing partial
                                int sector_length = 512 - ofs; //left over length
                                if(ofs > 0 || chunk_size < sector_length){
                                        //read full block(only from offset will be used)
                                        block_read(fs_device, sector, bh->data);
                                } else {
                                        memset(bh->data, 0, 512);
                                }
                                memcpy(bh->data+ofs, buffer, chunk_size); //update cache
                                block_write(fs_device, sector, bh->data);//update disk block
                        }

                        //update buffer_head hashmap & add to cache list
                        lock_acquire(&cache_lock);
                        hash_insert(&cache_hash,&bh->he);
			lock_release(&cache_lock);


			}
		} else{//cache hit(existing already)
			if(bh->being_used == true){
				bh->access=true;
				bh->dirty=true;
				memcpy(bh->data+ofs,buffer,chunk_size);
			} else {
				PANIC("Cache Write Error! Existing Cache invalid");
			}
		}
	}




//evict a  cache if cache is full(first one for simplicity)
struct buffer_head * cache_evict(){
	struct list_elem * l_elem = list_begin(&cache_list);
	struct buffer_head * bh;

	bh = list_entry (l_elem, struct buffer_head, le);
	if(bh!=NULL){
	//if dirty, flush
	if(bh-> dirty == true){
		block_write(fs_device, bh->sector, bh->data);
		bh->dirty = false;
	}
	hash_delete(&cache_hash,&bh->he);
	return bh;
	}
	//no element in list
	return NULL;
}


 
