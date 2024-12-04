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
#define MAX_FILE_SIZE               144384  // (13 + (1024/8)) * 1024 = 144,384 bytes
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
    char empty [512-MAX_NAME_SIZE-sizeof(bool)]; // we need to pad this here so they are all consistent sizes in the directory blocks
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
    uint16_t nameLen = strlen(name)+1;
    
    if (nameLen > MAX_NAME_SIZE) return false; // over the size limit
    for(int i = 0; i < nameLen-1; i++) {
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
    uint16_t blkNum = 0;


    if(!read_sd_block(&f.bytes, INODE_BITMAP_BLOCK)) { // attempt to read inode bitmap from disk
        fserror = FS_IO_ERROR;
        return 0;
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
    set_bit(f.bytes, blkNum); // marking the inode as allocated
    if(!write_sd_block(&f.bytes, INODE_BITMAP_BLOCK)) { // writing changes back to memeory
        fserror = FS_IO_ERROR;
        return 0;
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
            if (strcmp(b.blk[j].name, name) == 0){
                //printf("Found!\nDirectory Block: %d [%u]\n", i, FIRST_DIRECTORY_BLOCK+i);
                //printf("Directory Entry: %d [%lu]\n", j, DIRECTORIES_PER_BLOCK*i+j);
                //printf("file %s open status: %d\n",name, b.blk[j].isOpen);
                return b.blk[j].isOpen;
            } 
        }
    }
    return false;
}

// this finds the first free data block in the bitmap
// marks it as allocated and returns the block number
// returns 0 on failure. The block number returned is
// relative to the first data block
uint16_t get_free_data_block(void){
    bitmap f;
    uint16_t blkNum = 0;

    if(!read_sd_block(&f.bytes, DATA_BITMAP_BLOCK)) { // attempt to read inode bitmap from disk
        fserror = FS_IO_ERROR; // can't read from disk, set fserror & return 0
        return 0;
    }
    int length = sizeof(f.bytes) / sizeof(f.bytes[0]);
    for (int i = 0; i < length; i++)
    {
        if (first_free_bit(f.bytes[i]) != -1){
            blkNum = ((8*i)+first_free_bit(f.bytes[i]));
            break;
        }
    }
    set_bit(f.bytes, blkNum); // marking the block as allocated
    if(!write_sd_block(&f.bytes, DATA_BITMAP_BLOCK)) { // writing changes back to memeory
        fserror = FS_IO_ERROR;
        return 0;
    }
    return blkNum;
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
        return 0;
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
    c.blk[j].isOpen = false;
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
            if (strcmp(b.blk[j].name, name) == 0) {
                return true;
            }
        }
    }

    return false;
}

void fs_print_error(void) {
    switch(fserror) {
        case FS_NONE: {
            fprintf(stderr, "No error.\n");
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
    printf("Expecting sizeof(Inode) = 32, actual %d", sizeof(Inode));
    printf("Expecting sizeof(IndirectBlock) = 1024, actual = %d", sizeof(IndirectBlock));
    printf("Expecting sizeof(InodeBlock) = 1024, actual = %d", sizeof(InodeBlock));
    printf("Expecting sizeof(DirEntry) = 512, actual = %d", sizeof(DirectoryEntry));
    printf("Expecting sizeof(FreeBitmap) = 1024, actual = %d", sizeof(bitmap));

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
    //printf("File %s Found \n", name);

    // find the directory entry associated with the file
    DirectoryBlock b;
    int i, j; // we need these to calculate the inode num
    for (i = 0; i < 100; i++){ // there are seventy-five directory blocks
        bool found = false;
        read_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i);
        for (j = 0; j < DIRECTORIES_PER_BLOCK; j++){ // read through every element in the block
            if (strcmp(b.blk[j].name, name) == 0)
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
    b.blk[j].isOpen = true;

    if(!write_sd_block(b.blk, FIRST_DIRECTORY_BLOCK+i)){
        fserror = FS_IO_ERROR;
        return NULL;
    }

    //printf("file %s open status set to: %d\n",name, b.blk[j].isOpen);
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
    //printf("loading directory block: %d [%d]\n", dBlkNum, FIRST_DIRECTORY_BLOCK+dBlkNum);
    if(!read_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+dBlkNum)){
        fserror = FS_IO_ERROR;
        return NULL;
    }
    //printf("editing directory entry: %d [%d]\n", dBlkPos, inodeNum);
    //printf("%ld bytes were allocated for the directory entry name\n", sizeof(c.blk[dBlkPos].name));
    strcpy(c.blk[dBlkPos].name, name);
    //printf("set name to %s\n", c.blk[dBlkPos].name);
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

    if (! write_sd_block(c.blk, FIRST_DIRECTORY_BLOCK+dBlkNum)){ // writing directory entry changes back to memeory
        fserror = FS_IO_ERROR;
        return NULL;
    }

    // opening the file //
    File ret = open_file(name, READ_WRITE);

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
    if (!isOpen(file->name)) {
        fserror = FS_FILE_NOT_OPEN;
        return redbytes;
    }

    InodeBlock iBlk;
    if(!read_sd_block(iBlk.inodes, FIRST_INODE_BLOCK + file->inodeNum / INODES_PER_BLOCK)){
        fserror = FS_IO_ERROR;
        return redbytes;
    }
    
    char data[numbytes];

    // actually reading the inode data //
    Inode fileNode = iBlk.inodes[file->inodeNum%INODES_PER_BLOCK];
    //printf("entering main read loop\n");
    while(numbytes > 0){
        if(file->pos > fileNode.size) break; // this prevents reading past the end of the file

        if (file->pos > SOFTWARE_DISK_BLOCK_SIZE*NUM_DIRECT_INODE_BLOCKS){ // if its in the indirect block we have to do different operations
            IndirectBlock indirect;
            if(!read_sd_block(indirect.b, FIRST_DATA_BLOCK+fileNode.b[sizeof(fileNode.b) / sizeof(fileNode.b[0])])){ // this gets the indirect block
                fserror = FS_IO_ERROR;
                return redbytes;
            }

            //printf("reading in the direct blocks\n");
            char blk[SOFTWARE_DISK_BLOCK_SIZE];
            if(!read_sd_block(blk, FIRST_DATA_BLOCK+fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE])){
                fserror = FS_IO_ERROR;
                return redbytes;
            }

            uint32_t internalPos;
            internalPos = (file->pos % SOFTWARE_DISK_BLOCK_SIZE);

            //printf("Entering inner read loop\n");
            for (internalPos; internalPos < SOFTWARE_DISK_BLOCK_SIZE && numbytes != 0; internalPos++){
                if (fileNode.size++ > MAX_FILE_SIZE) { // this should only really happen in the indirect node section, but good practice to check
                    fserror = FS_EXCEEDS_MAX_FILE_SIZE;
                    return redbytes;
                }
                data[internalPos] = blk[redbytes];
                //printf("reading byte %d: %c", internalPos, blk[redbytes]);
                file->pos++;
                fileNode.size++;
                numbytes--;
                redbytes++;
            }

        }
        else{ // most commonly executed part, in theory
            //printf("reading in the direct blocks\n");
            char blk[SOFTWARE_DISK_BLOCK_SIZE];
            if(!read_sd_block(blk, FIRST_DATA_BLOCK+fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE])){
                fserror = FS_IO_ERROR;
                return redbytes;
            }

            uint32_t internalPos;
            internalPos = (file->pos % SOFTWARE_DISK_BLOCK_SIZE);

            //printf("Entering inner read loop\n");
            for (internalPos; internalPos < SOFTWARE_DISK_BLOCK_SIZE && numbytes != 0; internalPos++){
                if (fileNode.size++ > MAX_FILE_SIZE) { // this should only really happen in the indirect node section, but good practice to check
                    fserror = FS_EXCEEDS_MAX_FILE_SIZE;
                    return redbytes;
                }
                data[internalPos] = blk[redbytes];
                //printf("reading byte %d: %c", internalPos, blk[redbytes]);
                file->pos++;
                fileNode.size++;
                numbytes--;
                redbytes++;
            }
        }
    }

    // the return value is how many bytes are written; only necessary if the read is stopped prematurely
    //printf("%s\n", data);
    memcpy(buf, data, redbytes); // assigning this to point at our array containing the data
    //printf("%s\n", (char*)buf);
    return redbytes;
}

uint64_t write_file(File file, void *buf, uint64_t numbytes) {// return type is uint64_t for the same reason as above
    fserror = FS_NONE;
    uint64_t writbytes = 0;

    if (!isOpen(file->name)) {
        fserror = FS_FILE_NOT_OPEN; 
        return writbytes;
    }
    if (file->mode != READ_WRITE) {
        fserror = FS_FILE_READ_ONLY;
        return writbytes;
    }
    
   //printf("writing to %s\n", file->name);

    char input[numbytes];
    memcpy(input, buf, numbytes);

    InodeBlock iBlk;
    if(!read_sd_block(iBlk.inodes, file->inodeNum / INODES_PER_BLOCK)){
        fserror = FS_IO_ERROR;
        return writbytes;
    }

    //printf("Found the inode block: %d [%ld]\n", file->inodeNum, file->inodeNum/INODES_PER_BLOCK);

    // actually writing the data //
    Inode fileNode = iBlk.inodes[file->inodeNum%INODES_PER_BLOCK];

    //printf("entering write loop at position: %d\n", file->pos);
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

            char data[SOFTWARE_DISK_BLOCK_SIZE]; // the block we are currently in
            if(!read_sd_block(data, indirect.b[FIRST_DATA_BLOCK + ((file->pos/sizeof(SOFTWARE_DISK_BLOCK_SIZE))-NUM_DIRECT_INODE_BLOCKS)])){ // subtract NUM_DIRECT_INODE_BLOCKS here because otherwise it would never read the first NUM_DIRECT_INODE_BLOCKS blocks
                fserror = FS_IO_ERROR;
                return writbytes;
            }
            uint16_t internalPos = file->pos % SOFTWARE_DISK_BLOCK_SIZE;
            for (internalPos; internalPos < SOFTWARE_DISK_BLOCK_SIZE && numbytes != 0; internalPos++){
                if (fileNode.size++ > MAX_FILE_SIZE) {
                    fserror = FS_EXCEEDS_MAX_FILE_SIZE;
                    return writbytes;
                }
                data[internalPos] = input[writbytes]; // converting whatever is in buf to a char and putting it in data
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
            if (fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE] == 0){ // 0 is a reserved bit, not initialized
                //printf("allocating a new data block\n");
                fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE] = get_free_data_block();
                //printf("allocated block %d\n",fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE]);
            }
            // reading the data block in
            char data[SOFTWARE_DISK_BLOCK_SIZE];
            if(!read_sd_block(data, FIRST_DATA_BLOCK+fileNode.b[file->pos/SOFTWARE_DISK_BLOCK_SIZE])){
                fserror = FS_IO_ERROR;
                return writbytes;
            }
            //printf("block read in\n");

            uint32_t internalPos;
            //printf("File Position is: %d\n", file->pos);
            internalPos = (file->pos % SOFTWARE_DISK_BLOCK_SIZE);
            //printf("Internal Pos set: %d\n", internalPos);

            //printf("Entering the internal write loop\nNumbytes is %ld\n", numbytes);
            for (internalPos; internalPos < SOFTWARE_DISK_BLOCK_SIZE && numbytes != 0; internalPos++){
                if (fileNode.size++ > MAX_FILE_SIZE) { // this should only really happen in the indirect node section, but good practice to check
                    fserror = FS_EXCEEDS_MAX_FILE_SIZE;
                    return writbytes;
                }
                //printf("writing byte %ld from buffer\n", writbytes);
                data[internalPos] = input[writbytes]; // converting whatever is in buf to a char and putting it in data
                file->pos++;
                fileNode.size++;
                numbytes--;
                writbytes++;
            }
            //printf("Finished the internal write loop\n");
            //printf("%s\n", data);
            if (sizeof(data) != SOFTWARE_DISK_BLOCK_SIZE){
                //printf("padding the data\n");
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
