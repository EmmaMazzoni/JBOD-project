#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {

    	int bytes_left = len;
    	int bytes_read=0;
    	int buff_offset=0;
	//read bytes
    	while (bytes_left>0) {
        	bytes_read = read(fd, buf+buff_offset, bytes_left);
        	
        	if (bytes_read < 0) {
        		return false; //error reading
        	}
        	bytes_left -= bytes_read;
        	buff_offset += bytes_read;  // update buffer pointer
    	}

    	return true; //read passed
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
	int bytes_left = len;
	int bytes_written;	
	int buff_offset=0;	
	//start write
	while (bytes_left > 0) {
	
		bytes_written = write(fd, buf+buff_offset, bytes_left);
		
		if (bytes_written < 0) {
		    return false;  // failed
		}
		bytes_left -= bytes_written;
		buff_offset += bytes_written;
	    }

	    return true; //write passed
	}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
	//creates header
    	uint8_t header[HEADER_LEN];
    	if (!nread(sd, HEADER_LEN, (uint8_t*)header)) {
    		//read failed
        	return false;
    	}
    	
    	//sets up length, ret, and op
	uint16_t length;
    	memcpy(&length, &header, sizeof(uint16_t));
    	length = ntohs(length);
   	uint16_t ret_val;
    	memcpy(&ret_val,&header[6],sizeof(uint16_t));
    	*ret=ntohs(ret_val);
	uint32_t opcode;
    	memcpy(&opcode, &header[2], sizeof(uint32_t));
    	*op = ntohl(opcode);
    

    	if (length > HEADER_LEN) {
        // there is a block to read
        	if (!nread(sd, JBOD_BLOCK_SIZE, block)) {	
            		return false;
        }
    }


    
    return true;
}

/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
 
	//creates length
    	uint16_t length = HEADER_LEN;
    	uint32_t cmd=(op >> 14)&0x3F;

    	if (cmd == JBOD_WRITE_BLOCK) {
        	length += JBOD_BLOCK_SIZE;
    	}
    	//coverts length and op
	length = htons(length);
    	op = htonl(op);

	//creates header
    	uint8_t header[HEADER_LEN];
    	memcpy(header, &length, sizeof(uint16_t));
    	memcpy(header + sizeof(uint16_t), &op, sizeof(uint32_t));
    	
    	//if a write is needed 
	if(cmd==JBOD_WRITE_BLOCK){
		//combines header and block
		uint8_t header_block[HEADER_LEN+JBOD_BLOCK_SIZE];
		memcpy(header_block, &length, sizeof(uint16_t));
    		memcpy(header_block + sizeof(uint16_t), &op, sizeof(uint32_t));
    		memcpy(header_block + HEADER_LEN, block, JBOD_BLOCK_SIZE);
    		
    		//writes to the combined header and block
		if (!nwrite(sd, HEADER_LEN+JBOD_BLOCK_SIZE, header_block)) {
           		 return false;
        	}
	}
	//else just writes to header
	else{
    		if (!nwrite(sd, HEADER_LEN, header)) {
    			//write failed
        		return false;
    		}
	}
    	
    

    return true;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
    // Create socket
    if ((cli_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return false;
    }
    
    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); 

    // convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET,ip, &(serv_addr.sin_addr)) <= 0) { 
        return false;
    }
    

    // Connect to the server
    if (connect(cli_sd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        return false; //connection failed
    }

    return true;
}



/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {

	if (cli_sd != -1) {
        	close(cli_sd);
       		cli_sd = -1;
    	}
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {

    if (cli_sd == -1) {
        return -1; // Not connected to the server
    }

    // Send the operation to the server
    if (!send_packet(cli_sd, op, block)) {

        return -1; // Error sending packet
    }

    // Receive response from the server
    uint16_t ret;
    
    
    //printf("made it to line 239\n");

    if (!recv_packet(cli_sd, &op, &ret, block)) {

        return -1; // error receiving packet
    }
    

 
    if (ret != 0) {

        return -1; // operation failed
    }
    
    return 0;
}
