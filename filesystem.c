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

// Globals & Defines //

#define MAX_NAME_SIZE       256
#define MAX_FILES           300
#define DATA_BITMAP_BLOCK   0
#define INODE_BITMAP_BLOCk  1
#define FIRST_INODE_BLOCK   2
#define NUM_DIRECT_INODE_BLOCKS 13 //arbitrarily picked, might need to change
fserror = FS_NONE; // this is an external var pulled from filesystem.h


// structures //

// Type for one inode. structure must be 32 bytes long.
typedef struct Inode{
    uint32_t size; // 4 bytes
    uint16_t b[NUM_DIRECT_INODE_BLOCKS+1]; // 14 (13+1) uint16_t items is 28 bytes , 4 + 28 = 32
} Inode;

// type for blocks of Inodes. Structure must be
// SOFTWARE_DISK_BLOCK_SIZE bytes (software disk block size).
typedef struct InodeBlock{
    Inode inodes[SOFTWARE_DISK_BLOCK_SIZE/sizeof(Inode)];
}InodeBlock;


//////////////////////
// Helper Functions //
//////////////////////

// actually probably the most important function lol
void fs_print_error(void)
{
    switch(fserror)
    {
        case FS_NONE:
        {
            break;
        }
        case FS_OUT_OF_SPACE:
        {
            fprintf(stderr, "ERROR: most recent operation exceeded total software disk space\n");
            break;
        }
        case FS_FILE_NOT_OPEN:
        {
            fprintf(stderr, "ERROR: attempted to access file content for a file that was not opened\n");
            break;
        }
        case FS_FILE_OPEN:
        {
            fprintf(stderr, "ERROR: file is already open");
            break;
        }
        case FS_FILE_NOT_FOUND:
        {
            fprintf(stderr, "ERROR: file could not be found to perform operation\n");
            break;
        }
        case FS_FILE_READ_ONLY:
        {
            fprintf(stderr, "ERROR: attempted to execute or write to a read only file\n");
            break;
        }
        case FS_FILE_ALREADY_EXISTS:
        {
            fprintf(stderr, "ERROR: atempted to create a file that already exists\n");
            break;
        }
        case FS_EXCEEDS_MAX_FILE_SIZE:
        {
            fprintf(stderr, "ERROR: attempted seek or write operation would exceed maximum filesize\n");
            break;
        }
        case FS_ILLEGAL_FILENAME:
        {
            fprintf(stderr, "ERROR: cannot create a file whose name starts with NULL\n");
            break;
        }
        case FS_IO_ERROR:
        {
            fprintf(stderr,"ERROR: Critical Failure Occurred in IO system\n");
            break;
        }
    }
}

// TODO: Implement
bool seek_file(File file, uint64_t bytepos)
{
    fserror = FS_NONE;
}

// TODO: Implement
uint64_t file_length(File file)
{
    fserror = FS_NONE;
    // use the inodes(?) to find the file start and end
    // do basic subtraction
}

// TODO: Implement
bool file_exists(char *name)
{
    bool exists = false; // assume we are going to return false
    fserror = FS_NONE;

    // check the directory blocks ?
}

// set jth bit in a bitmap composed of 8-bit integers
void set_bit(unsigned char *bitmap, uint64_t j){
    bitmap[j/8] != (1 << (j%8));
}

// clear jth bit in a bitmap composed of 8-bit integers
void clear_bit(unsigned char *bitmap, uint64_t j){
    bitmap[j/8] &= ~(1 << (j%8));
}

// returns true if the jth bit is set in a bitmap of 8-bit integers,
//otherwise false
bool is_bit_set(unsigned char *bitmap, uint64_t j){
    return bitmap[j/8] & (1 << (j%8));
}

// TODO: Implement
bool delete_file(char *name)
{
    fserror = FS_NONE;
    if(file_exists(name))
    {
        // delete the file
        return true;
    }
    return false;
}

// TODO: Implement
bool check_structure_alignment(void)
{
    
}

// returns true if the given name is of valid format,
// returns false otherwise
bool valid_name(char *name)
{
    int nameLen = strlen(name);

    if (nameLen > MAX_NAME_SIZE) return false; // over the size limit

    if (name[nameLen] != '\0') return false; // not properly null terminated
    for(int i = 0; i < nameLen; i++)
    {
        if (!isprint(name[i])) return false; // contains a non-printable char
    }

    return true;
}



//////////////////////////////
// Main Operating Functions //
//////////////////////////////

// TODO: Implement
// the 'mode' referred to here is read/write [execute isn't tested here]
File open_file(char *name, FileMode mode)
{
    fserror = FS_NONE;
    if (!file_exists(name)) // do this now so we don't accidentally try to open a nonexistant file
    {
        fserror = FS_FILE_NOT_FOUND;
        return NULL;
    }
    // open the file
    // treat current fileposition as byte 0
}

// TODO: Implement
File create_file(char *name)
{
    fserror = FS_NONE;
    if (file_exists(name)) // can't make two files with the same name
    {
        fserror = FS_FILE_ALREADY_EXISTS;
        return NULL;
    }
    if(!valid_name(name))
    {
        fserror = FS_ILLEGAL_FILENAME;
        return NULL;
    }
    // TODO: implement creating the file
}

// TODO: Implement
void close_file(File file)
{
    fserror = FS_NONE;
    if(!file_exists(file)) //TODO: figure out how to get the name, since file_exists needs the name, not the pointer
    {
        fserror = FS_FILE_NOT_FOUND;
    }
    else
    {
        // close the file
    }

}

// TODO: Implement
// the uint64_t return value is how many bytes are written
// not important really, unless the read stops partway through
uint64_t read_file(File file, void *buf, uint64_t numbytes)
{
    fserror = FS_NONE;
    uint64_t redbytes = 0;
    if(!file_exists(file)) //TODO: figure out how to get the name of f, since file_exists needs the name, not the pointer
    {
        fserror = FS_FILE_NOT_FOUND;
        return redbytes; // this is essentially just return 0, but its consistent, so leave it
    }

    // TODO: actually grab the file

    uint64_t fSize = file_length(file);
    while (redbytes < numbytes) //probably a better way to do this
    {
        if (redbytes > fSize) break; // this stops us from reading past the end of a file
        // TODO: else read a byte at position readbytes into 'buf'
        redbytes++;
    }

    return redbytes;

}

// TODO: Implement
// return type is uint64_t for the same reason as above
uint64_t write_file(File file, void *buf, uint64_t numbytes)
{
    fserror = FS_NONE;
    if(!file_exists(file)) //TODO: figure out how to get the name of f, since file_exists needs the name, not the pointer
    {
        // create the file
    }
    // open the file
    // write to the file
    // note: while you are writing check to make sure the current write would not cause the file to exceed max size
}
