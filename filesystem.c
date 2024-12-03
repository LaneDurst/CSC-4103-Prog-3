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
#define MAX_FILES                   300     // max number of files the system can handle
#define MAX_FILE_SIZE               144384  // (14 + 128) * 1024 = 144384 bytes
#define DATA_BITMAP_BLOCK           0       // location of the data bitmap block on disk
#define INODE_BITMAP_BLOCK          1       // location of the inode bitmap block on disk
#define FIRST_INODE_BLOCK           2       // location of the first inode block on disk (there are 256 total)
#define FIRST_DIRECTORY_BLOCK       259     // location of the first directory block on disk (there are 150 total)
#define FIRST_DATA_BLOCK            410     // location of the first data block on disk
#define NUM_DIRECT_INODE_BLOCKS     13      // number of direct blocks per file
#define NUM_SINGLE_INDIRECT_BLOCKS  1       // number of indirect blocks per file
#define DIRECTORIES_PER_BLOCK       sizeof(DirectoryBlock)/sizeof(DirectoryEntry) // should be 2
#define INODES_PER_BLOCK            sizeof(InodeBlock)/sizeof(Inode) // should be 32

// filesystem error code set (set by each filesystem function)
FSError fserror;

////////////////
// Structures //
////////////////

// Type for Directory Entries.
typedef struct DirectoryEntry {
    char name[MAX_NAME_SIZE]; // file name
    bool isOpen;
    char empty [512-MAX_NAME_SIZE-sizeof(isOpen)]; // we need to pad this here so they are all consistent sizes in the directory blocks
} DirectoryEntry;

// Type for blocks of Directory Entries. Structure must be
// SOFTWARE_DISK_BLOCK_SIZE bytes (software disk block size).
// Each directory block includes 2 directory entries.
typedef struct DirectoryBlock {
    // because this is integer division and not float division, this should not give a decimal value
    // for number of directory entries per block
    DirectoryEntry blk[SOFTWARE_DISK_BLOCK_SIZE/sizeof(DirectoryEntry)]; // 2 directory entries per block, in this instance
} DirectoryBlock;

typedef struct FileInternals {
    char name[256];
    uint16_t inodeNum; // the inode associated with the file
    FileMode mode; // access type; 'READ_WRITE' or 'READ_ONLY'
    uint32_t pos; // current position in file
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
// otherwise, returns -1
uint8_t first_free_bit(uint8_t byte){
    for (int i = 0; i < 8; i++){
        if (!(byte & (1 <<i))){
            return i;
        }
    }
    return -1;
}

// Searches the Inode bitmap for the first free Inode, then sets 
// the Inode as taken & returns the Inode number. The inode number
// returned is relative to the start of the Inode Blocks.
uint16_t get_first_free_inode(void) {
    bitmap f;
    uint16_t blkNum = -1;


    if(!read_sd_block(&f.bytes, INODE_BITMAP_BLOCK)) { // attempt to read inode bitmap from disk
        fserror = FS_IO_ERROR;
        return -1;
    }
    else
    {
        int length = sizeof(f.bytes) / sizeof(f.bytes[0]);
        for (int i = 0; i < length; i++)
        {
            if (first_free_bit(f.bytes[i]) != -1){
                blkNum = ((8*i)+first_free_bit(f.bytes[i]));
                break;
            }
        }
    }
    set_bit(&f, blkNum); // marking the inode as allocated
    if(!write_sd_block(&f.bytes, INODE_BITMAP_BLOCK)) { // writing changes back to memeory
        fserror = FS_IO_ERROR;
        return -1;
    }
    return blkNum; // return the number of the first free inode or -1 upon fail
}

// scans directory entries for the corresponding file
// and checks if the isOpen value is True or False. This
// value is what is returned by the function.
bool isOpen(char *name){
    DirectoryBlock b;
    for (int i = 0; i < 150; i++){ // there are 150 directory blocks
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i);
        for (int j = 0; j < (sizeof(DirectoryBlock)/sizeof(DirectoryEntry)); j++){ // read through every element in the block
            if (b.blk[j].name == name) return b.blk[j].isOpen;
        }
    }
}

// this finds the first free data block in the bitmap
// marks it as allocated and returns the block number
// returns -1 on failure. The block number returned is
// relative to the first data block
uint16_t get_free_data_block(void){
    bitmap f;
    uint16_t blkNum = -1;

    if(!read_sd_block(&f.bytes, DATA_BITMAP_BLOCK)) { // attempt to read inode bitmap from disk
        fserror = FS_IO_ERROR; // can't read from disk, set fserror & return 0
        return -1;
    }
    int length = sizeof(f.bytes) / sizeof(f.bytes[0]);
    for (int i = 0; i < length; i++)
    {
        if (first_free_bit(f.bytes[i]) != -1){
            blkNum = ((8*i)+first_free_bit(f.bytes[i]));
            break;
        }
    }
    set_bit(&f, blkNum); // marking the block as allocated
    if(!write_sd_block(&f.bytes, DATA_BITMAP_BLOCK)) { // writing changes back to memeory
        fserror = FS_IO_ERROR;
        return -1;
    }
    return FIRST_DATA_BLOCK+blkNum;
}


/////////////////////////////
// Header Helper Functions //
/////////////////////////////

bool seek_file(File file, uint64_t bytepos) {// TODO: Implement!
    fserror = FS_NONE;
    if (file == NULL)
    {
        fserror = FS_FILE_NOT_FOUND;
        return false;
    }
    if (bytepos > MAX_FILE_SIZE)
    {
        fserror = FS_EXCEEDS_MAX_FILE_SIZE;
        return false;
    }

    // set current position in file using 'bytepos' relative to start of file
    file->pos = bytepos;
    
    return true;
}

uint64_t file_length(File file) {
    fserror = FS_NONE;

    InodeBlock iBlk;
    uint16_t iBlkNum = file->inodeNum/INODES_PER_BLOCK;
    uint16_t iBlkPos = file->inodeNum%INODES_PER_BLOCK;
    if(!read_sd_block(iBlk.inodes, FIRST_INODE_BLOCK+iBlkNum)){
        fserror = FS_IO_ERROR;
        return NULL;
    }
    return iBlk.inodes[iBlkPos].size;
}

bool delete_file(char *name) {
    fserror = FS_NONE;
    if(isOpen(name)){
        fserror = FS_FILE_OPEN;
        return false;
    }
    if (!file_exists(name)) {
        fserror = FS_FILE_NOT_FOUND;
        return false;
    }

    // find the directory entry associated with the file
    DirectoryBlock b;
    int i, j; // we need these to calculate the inode num
    for (i = 0; i < 100; i++){ // there are seventy-five directory blocks
        bool found = false;
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i);
        for (j = 0; j < DIRECTORIES_PER_BLOCK; j++){ // read through every element in the block
            if (b.blk[j].name == name)
            {
                found = true;
                break;
            }
        }
        if (found) break;
    }

    // zeroing the directory entry //
    DirectoryBlock c;
    if(!read_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+i)){
        fserror = FS_IO_ERROR;
        return false;
    }
    memset(c.blk[j].empty, 0, sizeof(c.blk[j].empty)); // should already be 0, but whatever
    memset(c.blk[j].name, 0, sizeof(c.blk[j].name));
    memset(c.blk[j].isOpen, 0, sizeof(c.blk[j].isOpen));
    if(!write_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+i)){
        fserror = FS_IO_ERROR;
        return false;
    }
    
    return true;
}

bool file_exists(char *name) {
    fserror = FS_NONE;

    // find the directory entry associated with the file (assuming one exists)
    DirectoryBlock b;
    for (int i = 0; i < 150; i++){ // there are 150 directory blocks
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i);
        for (int j = 0; j < DIRECTORIES_PER_BLOCK; j++){ // read through every element in the block
            if (b.blk[j].name == name) return true;
        }
    }

    return false;
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
    printf("bitmap Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(bitmap));
    printf("Inode Size is [%ld] bytes, should be [32] bytes.\n", sizeof(Inode));
    printf("Inode Block Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(InodeBlock));
    printf("Each Inode Block contains [%ld] inodes, should contain [32] inodes.\n", sizeof(InodeBlock)/sizeof(Inode));
    printf("Directory Entry Size is [%ld] bytes, should be [512] bytes.\n", sizeof(DirectoryEntry));
    printf("Directory Block Size is [%ld] bytes, should be [1024] bytes.\n", sizeof(DirectoryBlock));
    printf("Each Directory Block contains [%ld] directory entries, should be [2] directory entries.\n", sizeof(DirectoryBlock)/sizeof(DirectoryEntry));
    printf("========================================================================================\n");

    if (SOFTWARE_DISK_BLOCK_SIZE != 1024) return false;
    if (sizeof(bitmap) != SOFTWARE_DISK_BLOCK_SIZE) return false;
    if (sizeof(Inode) != 32) return false;
    if (sizeof(InodeBlock) != SOFTWARE_DISK_BLOCK_SIZE) return false;
    if (sizeof(InodeBlock)/sizeof(Inode) != 32) return false;
    if (sizeof(DirectoryEntry) != 512) return false;
    if (sizeof(DirectoryBlock) != SOFTWARE_DISK_BLOCK_SIZE) return false;
    if (sizeof(DirectoryBlock)/sizeof(DirectoryEntry) != 2) return false;
    // add more if necessary

    return true;
}

//////////////////////////////
// Main Operating Functions //
//////////////////////////////

File open_file(char *name, FileMode mode) { // the 'mode' referred to here is read/write [execute isn't tested here]
    fserror = FS_NONE;
    if (!file_exists(name)) { // do this now so we don't accidentally try to open a nonexistant file
        fserror = FS_FILE_NOT_FOUND;
        return NULL;
    }

    // find the directory entry associated with the file
    DirectoryBlock b;
    int i, j; // we need these to calculate the inode num
    for (i = 0; i < 100; i++){ // there are seventy-five directory blocks
        bool found = false;
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i);
        for (j = 0; j < DIRECTORIES_PER_BLOCK; j++){ // read through every element in the block
            if (b.blk[j].name == name)
            {
                found = true;
                break;
            }
        }
        if (found) break;
    }

    // setting up the file  //
    File f = malloc(sizeof(File)); // must malloc it so we can access it outside of this function
    f->mode = mode;
    strcpy(f->name, name);
    f->inodeNum = ((4*i)+j);
    f->pos = 0; // treat current file position as byte 0

    // setting the directory entry to open //
    DirectoryBlock c;
    if(!read_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+i)){
        fserror = FS_IO_ERROR;
        return NULL;
    }
    c.blk[j].isOpen = true;
    if(!write_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+i)){
        fserror = FS_IO_ERROR;
        return NULL;
    }

    return f;
}

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

    // setting up directory entry
    uint16_t inodeNum = get_first_free_inode();

    // adding the directory entry to the first free directory block //;
    uint16_t dBlkNum = inodeNum/DIRECTORIES_PER_BLOCK; // inode num and directory number should be equivalent at all times
    uint16_t dBlkPos = inodeNum%DIRECTORIES_PER_BLOCK;

    DirectoryBlock c;
    if(!read_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+dBlkNum)){
        fserror = FS_IO_ERROR;
        return NULL;
    }
    memcpy(c.blk[dBlkPos].name, name, sizeof(name));
    c.blk[dBlkPos].isOpen = false;

    // initializing the Inode Size Value //
    InodeBlock iBlk;
    uint16_t iBlkNum = inodeNum/INODES_PER_BLOCK;
    uint16_t iBlkPos = inodeNum%INODES_PER_BLOCK;
    if(!read_sd_block(iBlk.inodes, FIRST_INODE_BLOCK+iBlkNum)){
        fserror = FS_IO_ERROR;
        return NULL;
    }
    iBlk.inodes[iBlkPos].size = 0;
    if(!write_sd_block(iBlk.inodes, FIRST_INODE_BLOCK+iBlkNum)){
        fserror = FS_IO_ERROR;
        return NULL;
    }

    // opening the file //
    File ret = open_file(name, READ_WRITE);
    
    if (! write_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+dBlkNum)){ // writing directory entry changes back to memeory
        fserror = FS_IO_ERROR;
        return NULL;
    }

    return ret;
}

void close_file(File file) {
    fserror = FS_NONE;
    if(file == NULL) { // TODO: figure out how to get the name, since file_exists needs the name, not the pointer
        fserror = FS_FILE_NOT_OPEN;
    }
    else {
        uint16_t blkNum = file->inodeNum/DIRECTORIES_PER_BLOCK;
        uint16_t blkPos = file->inodeNum%DIRECTORIES_PER_BLOCK;
        DirectoryBlock b;
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+blkNum);
        b.blk[blkPos].isOpen = false;
        // we DO NOT set the name back to nothing in the directory entry, as we are only CLOSING the file, not DELETING it
        write_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+blkNum);
        memset(file, 0, sizeof(file));
        free(file);
        file = NULL;
    }
}

uint64_t read_file(File file, void *buf, uint64_t numbytes) { // the return value is how many bytes are actually read; only necessary if the read is stopped prematurely
    fserror = FS_NONE;
    uint64_t redbytes = 0;
    if (!is_open(file)) {
        fserror = FS_FILE_NOT_OPEN;
        return redbytes;
    }

    InodeBlock iBlk;
    if(!read_sd_block(iBlk.inodes, FIRST_INODE_BLOCK + file->inodeNum / INODES_PER_BLOCK)){
        fserror = FS_IO_ERROR;
        return redbytes;
    }
    
    char *data;
    data = malloc(numbytes); // holds the data until we assign it to buf;

    // actually reading the inode data //
    Inode fileNode = iBlk.inodes[file->inodeNum%INODES_PER_BLOCK];
    while(numbytes > 0){
        if(file->pos > fileNode.size) break; // this prevents reading past the end of the file

        if (file->pos > SOFTWARE_DISK_BLOCK_SIZE*NUM_DIRECT_INODE_BLOCKS){ // if its in the indirect block we have to do different operations
            IndirectBlock indirect;
            if(!read_sd_block(indirect.b, FIRST_DATA_BLOCK+fileNode.b[sizeof(fileNode.b) / sizeof(fileNode.b[0])])){ // this gets the indirect block
                fserror = FS_IO_ERROR;
                return redbytes;
            }

            void *blk[SOFTWARE_DISK_BLOCK_SIZE]; // the block we are currently in
            if(!read_sd_block(blk, indirect.b[FIRST_DATA_BLOCK + ((file->pos/sizeof(SOFTWARE_DISK_BLOCK_SIZE))-NUM_DIRECT_INODE_BLOCKS)])){ // subtract NUM_DIRECT_INODE_BLOCKS here because otherwise it would never read the first NUM_DIRECT_INODE_BLOCKS blocks
                fserror = FS_IO_ERROR;
                return redbytes;
            }
            data[redbytes] = blk[file->pos%SOFTWARE_DISK_BLOCK_SIZE];
            file->pos++;
            redbytes++;
            numbytes--;

        }
        else{ // most commonly executed part, in theory
            void *blk[SOFTWARE_DISK_BLOCK_SIZE]; // the block we are currently in
            if(!read_sd_block(blk, fileNode.b[FIRST_DATA_BLOCK + file->pos/sizeof(SOFTWARE_DISK_BLOCK_SIZE)])){
                fserror = FS_IO_ERROR;
                return redbytes;
            }
            data[redbytes] = blk[FIRST_DATA_BLOCK + file->pos%SOFTWARE_DISK_BLOCK_SIZE];
            file->pos++;
            redbytes++;
            numbytes--;
        }
    }

    // the return value is how many bytes are written; only necessary if the read is stopped prematurely
    buf = &data; // assigning this to point at our array containing the data
    return redbytes;
}

uint64_t write_file(File file, void *buf, uint64_t numbytes) {// return type is uint64_t for the same reason as above
    fserror = FS_NONE;
    uint64_t writbytes = 0;

    if (!isOpen(file)) {
        fserror = FS_FILE_NOT_OPEN; 
        return writbytes;
    }
    if (file->mode != READ_WRITE) {
        fserror = FS_FILE_READ_ONLY;
        return writbytes;
    }
    
    InodeBlock iBlk;
    if(!read_sd_block(iBlk.inodes, file->inodeNum / INODES_PER_BLOCK)){
        fserror = FS_IO_ERROR;
        return writbytes;
    }

    // actually writing the data //
    Inode fileNode = iBlk.inodes[file->inodeNum%INODES_PER_BLOCK];

    while(numbytes > 0){
        if(file->pos > MAX_FILE_SIZE){
            fserror = FS_EXCEEDS_MAX_FILE_SIZE;
            return writbytes;
        }// this prevents reading past the end of the file

        if (file->pos > SOFTWARE_DISK_BLOCK_SIZE*NUM_DIRECT_INODE_BLOCKS){ // if its in the indirect block we have to do different operations
            IndirectBlock indirect;
            if(!read_sd_block(indirect.b, FIRST_DATA_BLOCK+fileNode.b[sizeof(fileNode.b) / sizeof(fileNode.b[0])])){ // this gets the indirect block
                fserror = FS_IO_ERROR;
                return writbytes;
            }

            void *data[SOFTWARE_DISK_BLOCK_SIZE]; // the block we are currently in
            if(!read_sd_block(data, indirect.b[FIRST_DATA_BLOCK + ((file->pos/sizeof(SOFTWARE_DISK_BLOCK_SIZE))-NUM_DIRECT_INODE_BLOCKS)])){ // subtract NUM_DIRECT_INODE_BLOCKS here because otherwise it would never read the first NUM_DIRECT_INODE_BLOCKS blocks
                fserror = FS_IO_ERROR;
                return writbytes;
            }
            uint16_t internalPos = file->pos % SOFTWARE_DISK_BLOCK_SIZE;
            for (internalPos; internalPos < SOFTWARE_DISK_BLOCK_SIZE && numbytes != 0; internalPos++){
                if (fileNode.size++ > MAX_FILE_SIZE) { // this should only really happen in the indirect node section, but good practice to check
                    fserror = FS_EXCEEDS_MAX_FILE_SIZE;
                    return writbytes;
                }
                data[internalPos] = *((char*)data[writbytes]); // converting whatever is in buf to a char and putting it in data
                file->pos++;
                fileNode.size++;
                numbytes--;
            }

            if (numbytes == 0 && sizeof(data) != SOFTWARE_DISK_BLOCK_SIZE){
                uint64_t empty = SOFTWARE_DISK_BLOCK_SIZE - sizeof(data); // how many bytes have been left empty
                void* start = &data[empty];
                memset(start, '\0', empty);
            }

            if(!write_sd_block(data, indirect.b[FIRST_DATA_BLOCK + ((file->pos/sizeof(SOFTWARE_DISK_BLOCK_SIZE))-NUM_DIRECT_INODE_BLOCKS)])){ // subtract NUM_DIRECT_INODE_BLOCKS here because otherwise it would never read the first NUM_DIRECT_INODE_BLOCKS blocks
                fserror = FS_IO_ERROR;
                return writbytes;
            }
        }
        else{ // most commonly executed part, in theory

            // get the block we are currently in, or allocate one if needed //
            if (fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE] == -1){ // not initialized
                fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE] = get_free_data_block();
            }
            // reading the data block in
            void *data[SOFTWARE_DISK_BLOCK_SIZE];
            if(!read_sd_block(data, FIRST_DATA_BLOCK+fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE])){
                fserror = FS_IO_ERROR;
                return writbytes;
            }
            uint16_t internalPos = file->pos % SOFTWARE_DISK_BLOCK_SIZE;
            for (internalPos; internalPos < SOFTWARE_DISK_BLOCK_SIZE && numbytes != 0; internalPos++){
                if (fileNode.size++ > MAX_FILE_SIZE) { // this should only really happen in the indirect node section, but good practice to check
                    fserror = FS_EXCEEDS_MAX_FILE_SIZE;
                    return writbytes;
                }
                data[internalPos] = *((char*)data[writbytes]); // converting whatever is in buf to a char and putting it in data
                file->pos++;
                fileNode.size++;
                numbytes--;
            }

            if (numbytes == 0 && sizeof(data) != SOFTWARE_DISK_BLOCK_SIZE){
                uint64_t empty = SOFTWARE_DISK_BLOCK_SIZE - sizeof(data); // how many bytes have been left empty
                void* start = &data[empty];
                memset(start, '\0', empty);
            }

            // updating data block with new info, in memeory
            if(!write_sd_block(data, FIRST_DATA_BLOCK+fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE])){
                fserror = FS_IO_ERROR;
                return writbytes;
            }

        }
    }

    return writbytes;
}
