// THIS AND FORMATFS.C ARE THE ONLY FILES WE ARE EDITING
// These are all the operations we will be doing on the initalized software disk from formatfs.c
// Authors: Lane Durst and Connor Morris

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <ctype.h>

#include "softwaredisk.h"
#include "filesystem.h"

///////////////////////
// Globals & Defines //
///////////////////////

#define MAX_NAME_SIZE               256     // max number of chars in the file name (includes null char) 
#define MAX_FILES                   320     // max number of files the system can handle
#define MAX_FILE_SIZE               144384  // (14 + 128) * 1024 = 144384 bytes
#define DATA_BITMAP_BLOCK           0       // location of the data bitmap block on disk
#define INODE_BITMAP_BLOCK          1       // location of the inode bitmap block on disk
#define FIRST_INODE_BLOCK           2       // location of the first inode block on disk (there are 256 total)
#define FIRST_DIRECTORY_BLOCK       259     // location of the first directory block on disk (there are 5 total)
#define FIRST_DATA_BLOCK            265     // location of the first data block on disk
#define NUM_DIRECT_INODE_BLOCKS     13      // number of direct blocks per file
#define NUM_SINGLE_INDIRECT_BLOCKS  1       // number of indirect blocks per file

// filesystem error code set (set by each filesystem function)
FSError fserror;

////////////////
// Structures //
////////////////

typedef struct FileInternals {
    char name[256]; // file name
    uint32_t size; // file size
    bool isOpen; // is the file currently being accessed?
    FileMode mode; // access type; 'READ_WRITE' or 'READ_ONLY'
    uint32_t pos; // current position in file
    // add more if necessary
} FileInternals;

// Type for Directory Entries.
typedef struct DirectoryEntry {
    File f; // file's metadata
    uint16_t inodeNum; // inode num associated with the file [2 bytes]
    // add more if necessary
} DirectoryEntry;

// Type for blocks of Directory Entries. Structure must be
// SOFTWARE_DISK_BLOCK_SIZE bytes (software disk block size).
// Each directory block includes 3 directory entries.
typedef struct DirectoryBlock {
    // because this is integer division and not float division, this should not give a decimal value
    // for number of directory entries per block
    DirectoryEntry blk[SOFTWARE_DISK_BLOCK_SIZE/sizeof(DirectoryEntry)]; // 64 directory entries per block, in this instance
} DirectoryBlock;

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
typedef struct bitmap {
    uint8_t bytes[SOFTWARE_DISK_BLOCK_SIZE / sizeof(uint8_t)]; 
    // A bitmap should have 8192 bits, in theory, and be a full block in size.
} bitmap;

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
    bitmap f;

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
static bool mark_inode(uint16_t inode, bool flag) {
    fserror = FS_NONE;
    bitmap f;
    inode -= FIRST_INODE_BLOCK;
    if(! read_sd_block(&f, INODE_BITMAP_BLOCK)) {
        fserror = FS_IO_ERROR;
        return false;
    }
    else {
        if (flag) {
            // set bit
            f.bytes[inode / 8] != (1 << (7 - (inode % 8)));
        }
        else {
            // clear bit
            f.bytes[inode / 8] &= ~(1 << (7 - (inode % 8)));
        }
        if (! write_sd_block(&f, INODE_BITMAP_BLOCK)) {
            fserror = FS_IO_ERROR;
            return false;
        }
    }

    return true;
}

// finds the first free bit in a byte of data
// returns the bit number if there is a free bit
// otherwise, returns null
uint8_t first_free_bit(uint8_t byte)
{
    for (int i = 0; i < 8; i++){
        if (!(byte & (1 <<i))){
            return i;
        }
    }
    return -1;
}

// TODO: Check if the function fails correctly, because we can only return an integer
// Searches the Inode bitmap for the first free Inode, then sets 
// the Inode as taken & returns the Inode number. Sets fserror on I/O error.
uint16_t get_first_free_inode(void) {

    uint16_t failure = -1; // set fail state of -1 (equal to UINT16_MAX)
    bitmap f;
    
    if(!read_sd_block(&f.bytes, INODE_BITMAP_BLOCK)) { // attempt to read inode bitmap from disk
        fserror = FS_IO_ERROR; // can't read from disk, set fserror & return -1
    }
    else
    {
        int length = sizeof(f.bytes) / sizeof(f.bytes[0]);
        for (int i = 0; i < length; i++)
        {
            if (first_free_bit(f.bytes[i]) != -1) return ((8*i)+first_free_bit(f.bytes[i]));
        }
    }

    return failure; // return the number of the first free inode or -1 upon fail
}

// TODO: Implement!
// probably slow, might be a better way to do this
uint16_t get_first_free_dir(void) {
    // read through each block and check if there is a free dir entry space
    DirectoryBlock b;
    for (int i = 0; i < 5; i++){ // there are five directory blocks
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i);
        for (int j = 0; j < (sizeof(DirectoryBlock)/sizeof(DirectoryEntry)); j++){ // read through every element in the block
            if (b.blk[j].f->name == NULL) return ((64*i)+j);
        }
    }
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
    printf("================Structure=Alignment=====================================================\n");
    printf("Disk Block Size is [%d] bytes, should be [1024] bytes.\n", SOFTWARE_DISK_BLOCK_SIZE);
    printf("\nbitmap Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(bitmap));
    printf("\nInode Size is [%ld] bytes, should be [32] bytes.\n", sizeof(Inode));
    printf("Inode Block Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(InodeBlock));
    printf("Each Inode Block contains [%ld] inodes, should contain [32] inodes.\n", sizeof(InodeBlock)/sizeof(Inode));
    printf("\nDirectory Entry Size is [%ld] bytes, should be [16] bytes.\n", sizeof(DirectoryEntry));
    printf("Directory Block Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(DirectoryBlock));
    printf("Each Directory Block contains [%ld] directory entries, should be [64] directory entries.\n", sizeof(DirectoryBlock)/sizeof(DirectoryEntry));
    printf("========================================================================================\n");

    if (SOFTWARE_DISK_BLOCK_SIZE != 1024) return false;
    if (sizeof(Inode) != 32) return false;
    if (sizeof(InodeBlock) != SOFTWARE_DISK_BLOCK_SIZE) return false;
    if (sizeof(bitmap) != SOFTWARE_DISK_BLOCK_SIZE) return false;
    if (sizeof(DirectoryEntry) != 512) return false;
    if (sizeof(DirectoryBlock) != SOFTWARE_DISK_BLOCK_SIZE) return false;
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

    // find the directory entry associate with the file (assuming one exists)
    DirectoryBlock b;
    for (int i = 0; i < 5; i++){ // there are five directory blocks
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i);
        for (int j = 0; j < (sizeof(DirectoryBlock)/sizeof(DirectoryEntry)); j++){ // read through every element in the block
            if (b.blk[j].f->name == name) return ((64*i)+j);
        }
    }
    // add the directory entry to the first free directory block
    uint16_t raw = get_first_free_dir();
    uint16_t blkNum = raw/64; //because of the way int math works this should give a rounded down whole number
    uint16_t blkPos = raw%64;

    DirectoryBlock c;
    read_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+blkNum);
    c.blk[blkPos].f->isOpen = true;
    c.blk[blkPos].f->mode = mode;
    c.blk[blkPos].f->pos = 0;
    write_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+blkNum);
    
    // treat current file position as byte 0

    return c.blk[blkPos].f; // temporary return, change later
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
    
    // creating the file [incomplete] //
    bitmap b;
    if(! read_sd_block(b.bytes, INODE_BITMAP_BLOCK)) {
        fserror = FS_IO_ERROR;
        return NULL;
    }

    // setting up directory entry
    DirectoryEntry entry;
    strcpy(entry.f->name, name);
    entry.f->size = 0;
    entry.f->isOpen = false;
    entry.f->mode = READ_WRITE;
    entry.f->pos = 0;
    entry.inodeNum = get_first_free_inode();
    set_bit(b.bytes, entry.inodeNum); // actually marking the inode as taken
    if(! write_sd_block(b.bytes, INODE_BITMAP_BLOCK)) { // updating bitmap in memory
        fserror = FS_IO_ERROR;
        return NULL;
    }

    // add the directory entry to the first free directory block
    uint16_t raw = get_first_free_dir();
    uint16_t blkNum = raw/64; //because of the way int math works this should give a rounded down whole number
    uint16_t blkPos = raw%64;

    DirectoryBlock c;
    read_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+blkNum);
    memcpy(c.blk[blkPos].f, entry.f, sizeof(entry.f));
    memcpy(c.blk[blkPos].inodeNum, entry.inodeNum, sizeof(entry.inodeNum));
    write_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+blkNum);

    // opening the file
    File ret = open_file(name, READ_WRITE);

    return ret;
}

// TODO: Implement!
void close_file(File file) {
    fserror = FS_NONE;
    if(!(file->isOpen)) { // TODO: figure out how to get the name, since file_exists needs the name, not the pointer
        fserror = FS_FILE_NOT_OPEN;
    }
    else {
        file->isOpen = false;
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

    // part of this will actually require accessing the inode value associated with the file
    // and updating its array to include the new block we just wrote

    // instead of a for loop, should probably do a memcpy operation into the file location from buf for as many bytes as possible
    // write to the file
    // NOTE: while you are writing, check to make sure the current write would not cause the file to exceed max size

    return readbytes;
}
