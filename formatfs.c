// THIS AND FILESYSTEM.C ARE THE ONLY FILES WE ARE EDITING
// Filesystem initialized for CSC 4103 Programming Assignment #3
// Authors: Lane Durst and Connor Morris

#include <stdio.h>
#include "filesystem.c"
#include "softwaredisk.h"

void setup_disk(void) {
    // initializing data_bitmap
    bitmap data_bitmap;
    bzero(data_bitmap.bytes);
    write_sd_block(data_bitmap.bytes, DATA_BITMAP_BLOCK);

    // initializing inode_bitmap
    bitmap inode_bitmap;
    bzero(inode_bitmap.bytes);
    write_sd_block(inode_bitmap.bytes, INODE_BITMAP_BLOCK);
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