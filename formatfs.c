// THIS AND FILESYSTEM.C ARE THE ONLY FILES WE ARE EDITING
// Filesystem initialized for CSC 4103 Programming Assignment #3
// Authors: Lane Durst and Connor Morris

#include <stdio.h>

#include "softwaredisk.h"
#include "filesystem.h"

// NOTE: There is probably a better way to do this rather than just redefining the
// structure again, but i couldn't figure it out

// Type for one bitmap. Structure must be
// SOFTWARE_DISK_BLOCK_SIZE bytes (software disk block size).
typedef struct bitmap {
    uint8_t bytes[SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint8_t)]; 
    // A bitmap should have 8192 bits, in theory, and be a full block in size.
} bitmap;

void setup_disk(void) {
    // initializing data_bitmap
    bitmap data_bitmap;
    memset(data_bitmap.bytes, 0, sizeof(data_bitmap.bytes));
    write_sd_block(data_bitmap.bytes, 0);

    // initializing inode_bitmap
    bitmap inode_bitmap;
    memset(inode_bitmap.bytes, 0, sizeof(inode_bitmap.bytes));
    write_sd_block(inode_bitmap.bytes, 1);
}

int main(int argc, char* argv[]) {
    printf("Checking data structure sizes and alignments ... \n");
    if (!check_structure_alignment()) {
        printf("Check failed. Filesystem not initialized and should not be used.\n");
    }
    else {
        printf("Check succeeded.\n");

        printf("Initializing filesystem...\n");
        // my FS only simply requires a completely zeroed software disk
        init_software_disk();
        setup_disk(); // filesystem setup on the disk
        printf("Done.\n");
    }
    return 0;
}