#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"

static int mounted = 0;
static int write_access = 0;

int mdadm_mount(void) {
    if (mounted) {
        return -1;
    }

    uint32_t mount_command = JBOD_MOUNT << 12;
    if (jbod_client_operation(mount_command, NULL) == 0) {
        mounted = 1;
        return 1;
    }
    return -1;
}

int mdadm_unmount(void) {
    if (!mounted) {
        return -1;
    }

    uint32_t unmount_command = JBOD_UNMOUNT << 12;
    if (jbod_client_operation(unmount_command, NULL) == 0) {
        mounted = 0;
        return 1;
    }
    return -1;
}

int mdadm_write_permission(void) {
    if (mounted && !write_access) {
        jbod_client_operation(JBOD_WRITE_PERMISSION << 12, NULL);
        write_access = 1;
        return 1;
    }
    return -1;
}

int mdadm_revoke_write_permission(void) {
    if (write_access) {
        jbod_client_operation(JBOD_REVOKE_WRITE_PERMISSION << 12, NULL);
        write_access = 0;
        return 1;
    }
    return -1;
}

int mdadm_read(uint32_t address, uint32_t length, uint8_t *buffer) {
    // Check for invalid conditions
    if (address + length > JBOD_DISK_SIZE * JBOD_NUM_DISKS) {
        return -1; // Address out of bounds
    }
    if (length > 1024) {
        return -2; // Length exceeds maximum allowed
    }
    if (!mounted) {
        return -3; // Cannot read if not mounted
    }
    if (!buffer && length > 0) {
        return -4; // Buffer is null but length is non-zero
    }

    uint8_t tmp[JBOD_BLOCK_SIZE];
    uint8_t *arr = tmp;
    int disk_id = address / JBOD_DISK_SIZE;
    int block_id = (address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    uint8_t *ptr = buffer;
    int block1_offset = address % JBOD_BLOCK_SIZE;
    int current_address = address;
    int read = length;
    int flag = 0; // Tracks if the first block had an offset

    while (read > 0) {
        // Check cache for the block
        if (cache_lookup(disk_id, block_id, arr) != 1) {
            // Seek to the correct disk and block
            jbod_client_operation(JBOD_SEEK_TO_DISK << 12 | disk_id, NULL);
            jbod_client_operation(JBOD_SEEK_TO_BLOCK << 12 | block_id << 4, NULL);
            // Read the block into the temporary buffer
            jbod_client_operation(JBOD_READ_BLOCK << 12, tmp);
            // Insert the block into the cache
            cache_insert(disk_id, block_id, tmp);
        }

        // Handle reading from the first block with an offset
        if (block1_offset + read > JBOD_BLOCK_SIZE && flag == 0) {
            arr += block1_offset;
            memcpy(ptr, arr, JBOD_BLOCK_SIZE - block1_offset);
            ptr += JBOD_BLOCK_SIZE - block1_offset;
            current_address += JBOD_BLOCK_SIZE - block1_offset;
            read -= JBOD_BLOCK_SIZE - block1_offset;
            block1_offset = 0;
            flag = 1;
        }
        // Handle reading full blocks
        else if (flag == 1 && read > JBOD_BLOCK_SIZE) {
            memcpy(ptr, arr, JBOD_BLOCK_SIZE);
            ptr += JBOD_BLOCK_SIZE;
            current_address += JBOD_BLOCK_SIZE;
            read -= JBOD_BLOCK_SIZE;
        }
        // Handle reading the last block
        else if (read != 0 && read + block1_offset <= JBOD_BLOCK_SIZE) {
            arr += block1_offset;
            memcpy(ptr, arr, read);
            read = 0;
            return length; // Return the total length read
        }

        // Move to the next block
        arr = tmp;
        block_id++;
        if (block_id >= JBOD_NUM_BLOCKS_PER_DISK) {
            disk_id++;
            block_id = 0;
        }
    }

    return length; // Return the total length read
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {
    // Check for invalid input
    if ((write_buf == NULL) && (write_len == 0)) {
        return 0; // No data to write, return success
    }
    if (start_addr + write_len > JBOD_DISK_SIZE * JBOD_NUM_DISKS ) {
        return -1; // Address out of bounds
    }
    if (write_len > 1024) {
        return -2; // Write length exceeds maximum allowed
    }
    if (!mounted) {
        return -3; // Cannot write if not mounted
    }
    if ((write_buf == NULL) && (write_len > 0)) {
        return -4; // Buffer is null but length is non-zero
    }
    if (write_access == 0) {
        return -5; // Write access not granted
    }

    // Calculate initial disk and block addresses
    int disk_id = start_addr / JBOD_DISK_SIZE;
    int block_id = (start_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    int block_offset = start_addr % JBOD_BLOCK_SIZE;

    int bytes_remaining = write_len;
    int bytes_written = 0;
    uint8_t read_buf[JBOD_BLOCK_SIZE];

    while (bytes_remaining > 0) {
        // Seek to the correct disk and block
        jbod_client_operation(JBOD_SEEK_TO_DISK << 12 | disk_id, NULL);
        jbod_client_operation((JBOD_SEEK_TO_BLOCK << 12) | (block_id << 4), NULL);
        
        // Read the current block into the buffer
        jbod_client_operation((JBOD_READ_BLOCK << 12), read_buf);
      
        // Modify the read buffer with new data
        for (int i = block_offset; i < JBOD_BLOCK_SIZE && bytes_remaining > 0; i++) {
            read_buf[i] = *write_buf; 
            write_buf++;               
            bytes_remaining--;         
            bytes_written++;     
        }

        // Seek to the correct block again before writing
        jbod_client_operation((JBOD_SEEK_TO_BLOCK << 12) | (block_id << 4) | disk_id, NULL);
       
        // Write the modified buffer back to the block
        jbod_client_operation((JBOD_WRITE_BLOCK << 12), read_buf);
        
        // Insert the block into the cache, or update if it already exists
        if(cache_insert(disk_id, block_id, read_buf) == 0){
            cache_update(disk_id, block_id, read_buf);
        }

        // Increment block and disk IDs as needed
        block_id++;
        if(block_id >= JBOD_NUM_BLOCKS_PER_DISK){
            disk_id++;
            block_id = 0; // Reset block ID if end of disk is reached
        }
        block_offset = 0; // Reset block offset after the first iteration
    }

    return bytes_written; // Return the number of bytes written
}
