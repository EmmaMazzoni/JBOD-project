/* Author:  Emma Mazzoni
   Date: 4/7/24
    */
    
    
    
/***
 *      ______ .___  ___. .______     _______.  ______              ____    __   __  
 *     /      ||   \/   | |   _  \   /       | /      |            |___ \  /_ | /_ | 
 *    |  ,----'|  \  /  | |  |_)  | |   (----`|  ,----'              __) |  | |  | | 
 *    |  |     |  |\/|  | |   ___/   \   \    |  |                  |__ <   | |  | | 
 *    |  `----.|  |  |  | |  |   .----)   |   |  `----.             ___) |  | |  | | 
 *     \______||__|  |__| | _|   |_______/     \______|            |____/   |_|  |_| 
 *                                                                                   
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "util.h"
#include "jbod.h"
#include "cache.h"
#include "net.h"



int is_mounted=0;	//variable that changes to 1 when mounted and 0 when unmounted

uint32_t func(int cmd){	//passes the value of mounted or unmounted into the int
 	uint32_t op=0;
 	op+=cmd;
 	op=op<<14;
 	return op;	
  }

int mdadm_mount(void) {

	if(is_mounted==0){			//tests to make sure the equation is not already mounted
	uint32_t op=func(0);		//passes 0 into the function to mount the disk
	int x=jbod_client_operation(op, NULL); //uses the jbod operation to make sure it passes
	if(x==0){
	//if jbod returns 0 it passes
	is_mounted=1;			//sets mounted to 1 
	return 1;
	}
	else{	
	//printf("mount failed\n");			//if jbod returns 1 it fails
	is_mounted=0;
	return -1;
	}
	}
  else{

  	return -1;
  }

}

int mdadm_unmount(void) {
	  uint32_t op=func(1);			//passes one into the function to unmount 
	  if(is_mounted==1){			// makes sure the disk is mounted
		int x=jbod_client_operation(op, NULL); //passes the int into the function
		if(x==0){			//if the JBOD returns 0 it passes
		is_mounted=0;			//sets varibale to unmounted (0) 
		return 1;
		}
		else{				//otherwise it fails
		is_mounted=1;			
		return -1;
		}
		}
	  else{
	   	return -1;
	  }
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
	//doesnt allow read if buf is null and length is not 0

 	if(buf==NULL && len !=0){
 		return -1;
 	}
 	if(len==0){
 	return 0;
 	}
 	
 	//tests invalid parameters
  	if((is_mounted==1 && len<=1024 && addr<=1048576 && addr>=0 &&(addr+len)<=1048576)||(buf==NULL && len ==0)){ 
 		
 		uint8_t tempBuf[256];
 		
 		// Creates correct diskID and blockID based on adress
 		uint32_t diskID = addr / (JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE);
		uint32_t blockID = (addr % (JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE)) / JBOD_BLOCK_SIZE;
				
        	// Seek to the correct disk and block
 		uint32_t seekDiskOp = (diskID << 28) | (0 << 20) | (JBOD_SEEK_TO_DISK << 14);
        	jbod_client_operation(seekDiskOp, NULL);
        	
        	uint32_t seekBlockOp = (0 << 28) | (blockID << 20) | (JBOD_SEEK_TO_BLOCK << 14);
        	jbod_client_operation(seekBlockOp, NULL);
		
		
		int offset= addr % 256;		//variable to check position in block where address starts
		int read=0;			//variable to keep track of bytes read
		int block_position=blockID;	//variable to keep track of block position for later disk switching
        	// Read data 
        	while(read<=len-1){
        		
        		//Checks if cache is enabled if not proceeds as normal
        		if(cache_enabled()){
        		
        			//looks if block is already in cache and if it is reads from there
        			int found=cache_lookup(diskID, blockID, tempBuf);
        			if(found==-1){
        				//if not in cache puts block in cache and proceeds with read
        				uint32_t readOp = (0 << 28) | (0 << 20) | (JBOD_READ_BLOCK << 14);
        				jbod_client_operation(readOp, tempBuf);
        				cache_insert(diskID, blockID, tempBuf);
        				
        			}
        		}
        		else{
        		uint32_t readOp = (0 << 28) | (0 << 20) | (JBOD_READ_BLOCK << 14);
        		jbod_client_operation(readOp, tempBuf);
        		}
        		
        		//checks if the addr starts in the middle of a block
			if(offset==0){
				//if the amt needed to be read is greater than a whole block reads the entire next block
				if((len-read)>=256){
	    				memcpy(buf+read, tempBuf, 256);
	    				read+=256;
				}
				//otherwise only reads what needs to be read of block
				else{
	    				memcpy(buf+read, tempBuf, (len-read));
	    				read+=(len-read);
	    				}
					
			}
			//if it does start in the middel of a block
			else if(offset>0){
				//checks to see if what needs to be read is greater then the length left of the block starting at the offset position
				if((len-read)>=(256-offset)){
		    				memcpy(buf+read, tempBuf+offset, 256-offset);
		    				read+=(256-offset);

		    				
					}
				//otherwise reads only needed 
				else if((len-read)<(256-offset)){

		    				memcpy(buf+read, tempBuf+offset, (len-read));
		    				read+=((len-read));

		    				
		    				}
		    			offset=0; //sets offset to 0 since you are now moving to a new block
		    			}
		    					    		
		    		block_position+=1;	
		    		//checks if the block position is greater than 256-1 and if it is moves to the next disk
		    		if(block_position>256-1){
		    			diskID+=1;
		    			uint32_t seekDiskOp = (diskID << 28) | (0 << 20) | (JBOD_SEEK_TO_DISK << 14);
        				jbod_client_operation(seekDiskOp, NULL);
        				blockID=0;
        				block_position=0;
        				
		    		}
				
				      	
	     	}
	   	return len;
	      }
	    
	      //if parameters are invalid returns -1
  		return -1;
}
int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {

	if(buf==NULL && len !=0){
 		return -1;
 	}
	if((is_mounted==1 && len<=1024 && addr<=1048576 && addr>=0 &&(addr+len)<=1048576)||(buf==NULL && len ==0)){ 
		
	
		// Creates correct diskID and blockID based on adress
 		uint32_t diskID = addr / (JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE);
		uint32_t blockID = (addr % (JBOD_NUM_BLOCKS_PER_DISK * JBOD_BLOCK_SIZE)) / JBOD_BLOCK_SIZE;
				
        	// Seek to the correct disk and block
 		uint32_t seekDiskOp = (diskID << 28) | (0 << 20) | (JBOD_SEEK_TO_DISK << 14);
        	jbod_client_operation(seekDiskOp, NULL);
        	
        	uint32_t seekBlockOp = (0 << 28) | (blockID << 20) | (JBOD_SEEK_TO_BLOCK << 14);
        	jbod_client_operation(seekBlockOp, NULL);
        	
        	// Write data
	    	int offset = addr % JBOD_BLOCK_SIZE;  	// offset within the block
	    	int remaining = len;  		     	// bytes remaining to write

	    	while (remaining > 0) {
			uint8_t tempBuf[JBOD_BLOCK_SIZE];

			// read the existing block
			//Checks if cache is enabled if not proceeds as normal
        		if(cache_enabled()){
        		
        			//looks if block is already in cache and if it is reads from there
        			int found=cache_lookup(diskID, blockID, tempBuf);
        			if(found==-1){
        				//if not in cache puts block in cache and proceeds with read
        				uint32_t readOp = (0 << 28) | (0 << 20) | (JBOD_READ_BLOCK << 14);
        				jbod_client_operation(readOp, tempBuf);
        				cache_insert(diskID, blockID, tempBuf);
        				
        			}
        		}
        		else{
        		uint32_t readOp = (0 << 28) | (0 << 20) | (JBOD_READ_BLOCK << 14);
        		jbod_client_operation(readOp, tempBuf);
        		}
		
			//address starts at the beginning of a block
			if(offset==0){
				if(remaining>=256){
					memcpy(tempBuf, buf+(len-remaining), 256);
					remaining-=256;
				}
				else{
					memcpy(tempBuf, buf+(len-remaining), remaining);
					remaining=0;
				}
				
			}
			
			//starts in the middle of a block
			else if(offset>0){
			
				//checks to see if what needs to be written is greater then whats left in the block
				if(remaining>=(256-offset)){
		    				memcpy(tempBuf+offset, buf+(len-remaining) , 256-offset);
		    				remaining-=(256-offset);	    				
				}
				//otherwise written only needed 
				else if(remaining<(256-offset)){

		    				memcpy(tempBuf+offset,buf+(len-remaining), remaining);
		    				remaining=0;

		    				
		    				}
				offset=0; //sets offset to 0 since you are now moving to a new block	
			}
			

			//move back a block
			uint32_t seekBlockOp = (0 << 28) | (blockID << 20) | (JBOD_SEEK_TO_BLOCK << 14);
        		jbod_client_operation(seekBlockOp, NULL);
        		
        		// writes tempBuf into block
		    	uint32_t writeOp = (diskID << 28) | (blockID << 20) | (JBOD_WRITE_BLOCK << 14);
        		jbod_client_operation(writeOp, tempBuf);
        		
        		//checks if cache is enabled and if it is updates the cache
			if(cache_enabled()){
        			cache_update(diskID,blockID,tempBuf);
        		}
        		  	
        		//move to next block  	
        		blockID+=1;		
        		uint32_t seekBlockOp2 = (0 << 28) | (blockID << 20) | (JBOD_SEEK_TO_BLOCK << 14);
        		jbod_client_operation(seekBlockOp2, NULL);


		    	//checks if the block position is greater than 256-1 and if it is moves to the next disk
		    	if(blockID>256-1){
		    		diskID+=1;
		    		uint32_t seekDiskOp = (diskID << 28) | (0 << 20) | (JBOD_SEEK_TO_DISK << 14);
        			jbod_client_operation(seekDiskOp, NULL);
        			blockID=0;
        				
		    		}
	    }
		return len;
	}
	else{
		return -1;
	}
}
