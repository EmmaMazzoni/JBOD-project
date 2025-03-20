#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
	//cache already exists
	if (cache!=NULL){
		return -1;
	}
	//Invalid num_entries
	if(num_entries<2 || num_entries>4096){
		return -1;
	}
	//valid parameters
	else{
		//allocate space for num_entries cache
		cache = (cache_entry_t *)malloc(num_entries * sizeof(cache_entry_t));
		//check to ensure it succeeded
    		if (cache == NULL) {
        		return -1;
    		}
    		// Initialize cache entries
    		for (int i = 0; i < num_entries; i++) {
        		cache[i].valid = false;
        		cache[i].disk_num = -1; //invalid address placeholder
        		cache[i].block_num = -1; //invalid address placeholder
        		cache[i].access_time = 0;
    		}
    		//update cache size
    		cache_size = num_entries;
    		
    		return 1;
    
	}
}

int cache_destroy(void) {
	if (cache == NULL) {
        //you cannot destroy a cache that doesn't exist
        	return -1;
    	}

    	// free memory
    	free(cache);

    	// reset values
    	cache = NULL;
    	cache_size = 0;

    	return 1; 

}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
	if (cache == NULL) {
        return -1; // cache not initialized
    	}
    	
	//checks invalid parameters
	if (buf == NULL || disk_num>16 || block_num>256 || disk_num<0 || block_num<0) {
        return -1;
    	}

    	num_queries++; //query is being performed add one

    	// look up block 
    	for (int i = 0; i < cache_size; i++) {
        	if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
          	
          		//copy buff into mem	
           		 memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
            		num_hits++;

            		// update access time and clock
            		cache[i].access_time = ++clock;

            		return 1; 
        	}
    	}

    	return -1; 
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {

	if (cache == NULL) {
        return; // cache not initialized
    	}
    	
	//buf cannot be NULL
	if (buf == NULL || disk_num>16 || block_num>256 || disk_num<0 || block_num<0) {
        return;
    }

    // update the block content if the entry exists
    for (int i = 0; i < cache_size; i++) {
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            //update its block content
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
            
            // update access time and clock
            cache[i].access_time = ++clock;
            
            return;
        }
    }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {

	if (cache == NULL) {
        return -1; // cache not initialized
    	}
    	
	//buf cannot be NULL
	if (buf == NULL || disk_num>16 || block_num>256 || disk_num<0 || block_num<0) {
        return -1;
    	}
    	
    	// check if entry already exists in the cache
    	for (int i = 0; i < cache_size; i++) {
        	if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            		return -1; 
        	}
    	}

    	// Find the LRU entry
    	int lru_index = 0;
    	int min_access_time = cache[0].access_time;
        for (int i = 1; i < cache_size; i++) {
        	if (cache[i].access_time < min_access_time) {
            		min_access_time = cache[i].access_time;
            		lru_index = i;
        	}
    	}


    	// copy block to the cache
    	memcpy(cache[lru_index].block, buf, JBOD_BLOCK_SIZE);
    	cache[lru_index].valid = true;
    	cache[lru_index].disk_num = disk_num;
    	cache[lru_index].block_num = block_num;
    
    	// update access time and clock
    	cache[lru_index].access_time = ++clock;

    	return 1;
}

bool cache_enabled(void) {
  return cache_size>2;
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
