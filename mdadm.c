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

static int mounted_status = 0;
static int write_access_granted = 0;

int mdadm_mount(void) {
    if (mounted_status) {
        return -1;
    }

    uint32_t mount_command = JBOD_MOUNT << 12;
    if (jbod_client_operation(mount_command, NULL) == 0) {
        mounted_status = 1;
        return 1;
    }
    return -1;
}

int mdadm_unmount(void) {
    if (!mounted_status) {
        return -1;
    }

    uint32_t unmount_command = JBOD_UNMOUNT << 12;
    if (jbod_client_operation(unmount_command, NULL) == 0) {
        mounted_status = 0;
        return 1;
    }
    return -1;
}

int mdadm_write_permission(void) {
    if (mounted_status && !write_access_granted) {
        jbod_client_operation(JBOD_WRITE_PERMISSION << 12, NULL);
        write_access_granted = 1;
        return 1;
    }
    return -1;
}

int mdadm_revoke_write_permission(void) {
    if (write_access_granted) {
        jbod_client_operation(JBOD_REVOKE_WRITE_PERMISSION << 12, NULL);
        write_access_granted = 0;
        return 1;
    }
    return -1;
}

int mdadm_read(uint32_t address, uint32_t length, uint8_t *buffer) {
    if (address + length > JBOD_DISK_SIZE * JBOD_NUM_DISKS || length > 1024 || !mounted_status || (!buffer && length > 0)) {
        return -1;
    }

    int current_disk = address / JBOD_DISK_SIZE;
    int current_block = (address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    int block_offset = address % JBOD_BLOCK_SIZE;

    uint8_t temp_buffer[256];
    uint8_t *buffer_position = buffer;
    int bytes_remaining = length;

    while (bytes_remaining > 0) {
        if (cache_lookup(current_disk, current_block, temp_buffer) != 1) {
            jbod_client_operation(JBOD_SEEK_TO_DISK << 12 | current_disk, NULL);
            jbod_client_operation(JBOD_SEEK_TO_BLOCK << 12 | current_block, NULL);
            jbod_client_operation(JBOD_READ_BLOCK << 12, temp_buffer);
            cache_insert(current_disk, current_block, temp_buffer);
        }

        int bytes_to_copy = JBOD_BLOCK_SIZE - block_offset;
        if (bytes_to_copy > bytes_remaining) {
            bytes_to_copy = bytes_remaining;
        }

        memcpy(buffer_position, temp_buffer + block_offset, bytes_to_copy);
        buffer_position += bytes_to_copy;
        bytes_remaining -= bytes_to_copy;

        block_offset = 0;
        current_block++;
        if (current_block >= JBOD_NUM_BLOCKS_PER_DISK) {
            current_block = 0;
            current_disk++;
        }
    }

    return length;
}

int mdadm_write(uint32_t address, uint32_t length, const uint8_t *buffer) {
    if ((!buffer && length == 0) || address + length > JBOD_DISK_SIZE * JBOD_NUM_DISKS || length > 1024 || !mounted_status || (!buffer && length > 0) || !write_access_granted) {
        return -1;
    }

    int current_disk = address / JBOD_DISK_SIZE;
    int current_block = (address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    int block_offset = address % JBOD_BLOCK_SIZE;

    uint8_t temp_buffer[256];
    int bytes_remaining = length;
    int total_written = 0;

    while (bytes_remaining > 0) {
        if (cache_lookup(current_disk, current_block, temp_buffer) != 1) {
            jbod_client_operation(JBOD_SEEK_TO_DISK << 12 | current_disk, NULL);
            jbod_client_operation(JBOD_SEEK_TO_BLOCK << 12 | current_block, NULL);
            jbod_client_operation(JBOD_READ_BLOCK << 12, temp_buffer);
        }

        int bytes_to_copy = JBOD_BLOCK_SIZE - block_offset;
        if (bytes_to_copy > bytes_remaining) {
            bytes_to_copy = bytes_remaining;
        }

        memcpy(temp_buffer + block_offset, buffer, bytes_to_copy);
        buffer += bytes_to_copy;
        bytes_remaining -= bytes_to_copy;
        total_written += bytes_to_copy;

        jbod_client_operation(JBOD_SEEK_TO_DISK << 12 | current_disk, NULL);
        jbod_client_operation(JBOD_SEEK_TO_BLOCK << 12 | current_block, NULL);
        jbod_client_operation(JBOD_WRITE_BLOCK << 12, temp_buffer);

        cache_insert(current_disk, current_block, temp_buffer);

        block_offset = 0;
        current_block++;
        if (current_block >= JBOD_NUM_BLOCKS_PER_DISK) {
            current_block = 0;
            current_disk++;
        }
    }

    return total_written;
}

