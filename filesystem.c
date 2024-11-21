// THIS AND FORMATFS.C ARE THE ONLY FILES WE ARE EDITING [Lane] //
// These are all the operations we will be doing on the initalized software disk from formatfs.c //

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>

#include "filesystem.h"

FSError most_recent_error = FS_NONE;

/* Helper Functions */

// actually probably the most important function
void fs_print_error(void)
{
    switch(most_recent_error)
    {
        case FS_NONE:
        {
            fprintf(stderr, "ERROR: an error was thrown, but error type was not set\n");
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

bool seek_file(File file, uint64_t bytepos)
{

}

uint64_t file_length(File file)
{
    // use the inodes(?) to find the file start and end
    // do basic subtraction
}

bool file_exists(char *name)
{

}

bool delete_file(char *name)
{
    if(file_exists(name))
    {
        // delete the file
        return true;
    }
    return false;
}

bool check_structure_alignment(void)
{
    
}

/*Open|Create & Deletion*/

File open_file(char *name, FileMode mode) // the 'mode' referred to here is read/write/execute
{
    if (!file_exists(name))
    {
        fs_print_error();
        return NULL; // do this now so we don't accidentally open a nonexistant file
    }
    // open the file
    // treat current fileposition as byte 0
}

// this should only be called if a file we are trying to write to does not already exist
File create_file(char *name)
{
    // TODO: implement creating the file
}

File close_file(File file) // supposed to 'set fserror global', whatever that means
{
    if(!file_exists(file)) //TODO: figure out how to get the name of f, since file_exists needs the name, not the pointer
    {
        // send an error
    }
    else
    {
        // close the file
    }

}

/* Read & Write */

// the uint64_t return value is how many bytes are written
// not important really, unless the read fails partway through
uint64_t read_file(File file, void *buf, uint64_t numbytes)
{
    if(!file_exists(file)) //TODO: figure out how to get the name of f, since file_exists needs the name, not the pointer
    {
        // send an error saying you can't find the file
        return NULL;
    }
    // grab the file
    uint64_t readbytes = 0;
    while (readbytes < numbytes) //probably a better way to do this
    {
        // read a byte at position readbytes in the file
    }
    return readbytes;

}

// return type is uint64_t for the same reason as above
uint64_t write_file(File file, void *buf, uint64_t numbytes)
{
    if(!file_exists(file)) //TODO: figure out how to get the name of f, since file_exists needs the name, not the pointer
    {
        // create the file
    }
    // open the file
    // write to the file
}
