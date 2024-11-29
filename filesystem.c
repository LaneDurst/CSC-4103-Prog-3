// THIS AND FORMATFS.C ARE THE ONLY FILES WE ARE EDITING //
// These are all the operations we will be doing on the initalized software disk from formatfs.c //

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <ctype.h>

#include "filesystem.h"
#include "softwaredisk.h"

///////////////////////
// Globals & Defines //
///////////////////////

#define MAX_NAME_SIZE               256
#define MAX_FILES                   300
#define MAX_FILE_SIZE               144384
#define DATA_BITMAP_BLOCK           0
#define INODE_BITMAP_BLOCK          1
#define FIRST_INODE_BLOCK           2
#define FIRST_DATA_BLOCK            1   // note: unsure of value
#define NUM_DIRECT_INODE_BLOCKS     13
#define NUM_SINGLE_INDIRECT_BLOCKS  1

// extern var, defined in filesystem.h
FSError fserror = FS_NONE; // initially set to FS_NONE to prevent issues on startup

////////////////
// Structures //
////////////////

typedef struct FileInternals {
    char* filename;
    FileMode mode;
    uint64_t size;
    bool isOpen;
    // add more if necessary
} FileInternals;

// Type for one inode. Structure must be 32 bytes long.
typedef struct Inode {
    uint32_t size; // file size in bytes [uint32_t = 4 bytes]
    uint16_t b[NUM_DIRECT_INODE_BLOCKS + NUM_SINGLE_INDIRECT_BLOCKS]; // 13 direct blocks + 1 indirect block
    // 14 (13 + 1) uint16_t items is 28 bytes, 4 + 28 = 32
} Inode;

// Type for blocks of Inodes. Structure must be
// SOFTWARE_DISK_BLOCK_SIZE bytes (software disk block size).
typedef struct InodeBlock {
    Inode inodes[SOFTWARE_DISK_BLOCK_SIZE / sizeof(Inode)]; 
    // Ideally, this is an array of 32 inodes so as to cleanly fit into a 1024 byte-sized block.
} InodeBlock;

// Type for one indirect block. Structure must be
// SOFTWARE_DISK_BLOCK_SIZE bytes (software disk block size). Max
// file size calculation is based on use of uint16_t as type for
// numbers.
typedef struct IndirectBlock {
    uint16_t b[NUM_SINGLE_INDIRECT_BLOCKS];
    // add more...?
} IndirectBlock;

// Type for one bitmap. Structure must be
// SOFTWARE_DISK_BLOCK_SIZE bytes (software disk block size).
typedef struct FreeBitmap {
    uint8_t bytes[SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint8_t)]; 
    // A bitmap should have 128 elements, in theory, and be a full block in size.
} FreeBitmap;

/////////////////////////////
// Custom Helper Functions //
/////////////////////////////

// set jth bit in a bitmap composed of 8-bit integers
void set_bit(unsigned char *bitmap, uint64_t j) {
    bitmap[j/8] != (1 << (j%8));
}

// clear jth bit in a bitmap composed of 8-bit integers
void clear_bit(unsigned char *bitmap, uint64_t j) {
    bitmap[j/8] &= ~(1 << (j%8));
}

// returns true if the jth bit is set in a bitmap of 8-bit integers,
// otherwise false
bool is_bit_set(unsigned char *bitmap, uint64_t j) {
    return bitmap[j/8] & (1 << (j%8));
}

// returns true if the given name is of valid format; otherwise, returns false
bool valid_name(char *name) {
    int nameLen = strlen(name);
    
    if (nameLen > MAX_NAME_SIZE) return false; // over the size limit
    if (name[nameLen] != '\0') return false; // not properly null terminated
    for(int i = 0; i < nameLen; i++) {
        if (!isprint(name[i])) return false; // contains a non-printable char
    }
    
    return true;
}

// mark block 'blk' allocated (1) or free (0) depending on 'flag'.
// Returns false on error and true on success.
static bool mark_block(uint16_t blk, bool flag) {

    FreeBitmap f;

    blk -= FIRST_DATA_BLOCK;
    if(! read_sd_block(&f, DATA_BITMAP_BLOCK)) {
        return false;
    }
    else {
        if (flag) {
            // set bit
            f.bytes[blk / 8] != (1 << (7 - (blk % 8)));
        }
        else {
            // clear bit
            f.bytes[blk / 8] &= ~(1 << (7 - (blk % 8)));
        }
        if (! write_sd_block(&f, DATA_BITMAP_BLOCK)) {
            return false;
        }
    }

    return true;
}

// mark inode 'i' allocated (1) or free (0) depending on 'flag'.
// Returns false on error and true on success. Sets fserror on I/O error.
static bool mark_inode(Inode i, bool flag) {
    // TODO: Implement!
}


/////////////////////////////
// Header Helper Functions //
/////////////////////////////

// TODO: Implement!
bool seek_file(File file, uint64_t bytepos) {
    fserror = FS_NONE;
    bool success = false; // return value for the function; assume we are going to return false

    // set current position in file using 'bytepos' relative to start of file
    
    return success;
}

// TODO: Implement!
uint64_t file_length(File file) {
    fserror = FS_NONE;

    // use the inodes(?) to find the file start and end
    // do basic subtraction

    return 0; // temporary return value, change later
}

// TODO: Implement!
bool delete_file(char *name) {
    fserror = FS_NONE;
    if (file_exists(name)) {
        
        // delete the file
        
        return true;
    }
    return false;
}

// TODO: Implement!
bool file_exists(char *name) {
    fserror = FS_NONE;
    bool exists = false; // return value for the function; assume we are going to return false

    // check the directory blocks?

    return exists;
}

void fs_print_error(void) {
    switch(fserror) {
        case FS_NONE: {
            break;
        }
        case FS_OUT_OF_SPACE: {
            fprintf(stderr, "ERROR: Most recent operation exceeded total software disk space!\n");
            break;
        }
        case FS_FILE_NOT_OPEN: {
            fprintf(stderr, "ERROR: Attempted to access file content for a file that was not opened!\n");
            break;
        }
        case FS_FILE_OPEN: {
            fprintf(stderr, "ERROR: File is already open!\n");
            break;
        }
        case FS_FILE_NOT_FOUND: {
            fprintf(stderr, "ERROR: File could not be found to perform operation!\n");
            break;
        }
        case FS_FILE_READ_ONLY: {
            fprintf(stderr, "ERROR: Attempted to execute or write to a read-only file!\n");
            break;
        }
        case FS_FILE_ALREADY_EXISTS: {
            fprintf(stderr, "ERROR: Atempted to create a file that already exists!\n");
            break;
        }
        case FS_EXCEEDS_MAX_FILE_SIZE: {
            fprintf(stderr, "ERROR: Attempted a seek or write operation that would exceed the maximum filesize!\n");
            break;
        }
        case FS_ILLEGAL_FILENAME: {
            fprintf(stderr, "ERROR: Cannot create a file whose name starts with NULL!\n");
            break;
        }
        case FS_IO_ERROR: {
            fprintf(stderr,"ERROR: Critical failure occurred in I/O system\n");
            break;
        }
    }
}

bool check_structure_alignment(void) {
    printf("================Structure=Alignment====================\n");
    printf("Disk Block Size is [%d] bytes, should be [1024] bytes.\n", SOFTWARE_DISK_BLOCK_SIZE);
    printf("Inode Size is [%ld] bytes, should be [32] bytes.\n", sizeof(Inode));
    printf("Inode Block Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(InodeBlock));
    printf("Bitmap Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(FreeBitmap));
    printf("=======================================================\n");

    if (SOFTWARE_DISK_BLOCK_SIZE != 1024) return false;
    if (sizeof(Inode) != 32) return false;
    if (sizeof(InodeBlock) != SOFTWARE_DISK_BLOCK_SIZE) return false;
    if (sizeof(FreeBitmap) != SOFTWARE_DISK_BLOCK_SIZE) return false;
    // add more if necessary

    return true;
}

//////////////////////////////
// Main Operating Functions //
//////////////////////////////

// TODO: Implement!
File open_file(char *name, FileMode mode) { // the 'mode' referred to here is read/write [execute isn't tested here]
    fserror = FS_NONE;
    if (!file_exists(name)) { // do this now so we don't accidentally try to open a nonexistant file
        fserror = FS_FILE_NOT_FOUND;
        return NULL;
    }

    // open the file
    
    // treat current fileposition as byte 0

    return NULL; // temporary return, change later
}

// TODO: Implement!
File create_file(char *name) {
    fserror = FS_NONE;
    if(!valid_name(name)) { // first, check that the new filename is valid
        fserror = FS_ILLEGAL_FILENAME;
        return NULL;
    }
    if (file_exists(name)) { // second, make sure that this file isn't a duplicate
        fserror = FS_FILE_ALREADY_EXISTS;
        return NULL;
    }
    
    // creating the file [incomplete]
    File f;
    strcpy(f->filename, name);
    f->isOpen = false;

    // opening the file
    f = open_file(name, READ_ONLY);

    return f;
}

// TODO: Implement!
void close_file(File file) {
    fserror = FS_NONE;
    if(!(file->isOpen)) { // TODO: figure out how to get the name, since file_exists needs the name, not the pointer
        fserror = FS_FILE_NOT_OPEN;
    }
    else {
        // close the file
    }
}

// TODO: Implement!
// the return value is how many bytes are written; only necessary if the read is stopped prematurely
uint64_t read_file(File file, void *buf, uint64_t numbytes) {
    fserror = FS_NONE;
    uint64_t readbytes = 0;

    if (!(file->isOpen)) {
        fserror = FS_FILE_NOT_OPEN;
        return numbytes;
    }

    // TODO: actually grab the file

    uint64_t fSize = file_length(file);
    while (readbytes < numbytes) { // probably a better way to do this
        if (readbytes > fSize) break; // this stops us from reading past the end of a file
        // TODO: else read a byte at position readbytes into 'buf'
        readbytes++;
    }

    return readbytes;
}

// TODO: Implement!
// return type is uint64_t for the same reason as above
uint64_t write_file(File file, void *buf, uint64_t numbytes) {
    fserror = FS_NONE;
    uint64_t readbytes = 0;

    if (!(file->isOpen)) {
        fserror = FS_FILE_NOT_OPEN; 
        return readbytes;
    }
    if (file->mode != READ_WRITE) {
        fserror = FS_FILE_READ_ONLY;
        return readbytes;
    }
    if ((file->size + numbytes) > MAX_FILE_SIZE) { // this will need to be changed, it should write as much as it can, until this happens
        fserror = FS_EXCEEDS_MAX_FILE_SIZE;
        return readbytes;
    }

    // instead of a for loop, should probably do a memcpy operation into the file location from buf for as many bytes as possible
    // write to the file
    // NOTE: while you are writing, check to make sure the current write would not cause the file to exceed max size

    return readbytes;
}
