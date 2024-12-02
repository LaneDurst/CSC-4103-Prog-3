# Design Document
Lane Durst & Connor Morris

## Block Allocation
- block 0 is allocated for the Data Bitmap
- block 1 is allocated for the Inode Bitmap
- blocks 2-258 are allocated for Inode Blocks
- blocks 259-264 are allocated for directory entries
- blocks 265-8192 are allocated for data storage

## Bitmap structure
There are two bitmaps contained in this filesystem.c implementation, the data bitmap and the inode bitmap. Their purpose is to log which data blocks and which inodes are free to be altered within the software disk. The bitmap structure is composed of an array of uint8_t elements, and is equal in size to an entire software disk block. In this case, that means 1024 uint8_t elements. However, these elements are not what operations on the bitmap affect directly. Instead, each inode or data block represented by a bitmap is represented by each individual bit of these elemenets. This means, for instance, that inode number 12 is not represented by the 12th element in the array, but is instead the 4th bit of the 2nd element of the array.

## Directory Entry Strucutre
The directory entry structure is composed of one part, a refrence to the file. In this case that is the name associated with the file. We do not store the inode number in the directory entry as the directory entry number and the inode number are always equivalent in this implementation.

### Note on Directory Blocks
[This needs to be updated]
Because the structure of a directory entry causes it to not evenly fit into a software disk block, when a directory block is initialized only 3 entries are placed into the block. The remaining space is then filled with an array of uint8_t elements, appropriately named 'empty'. No operations are performed using these bytes, they exist solely to ensure structure alignment

## Implementation limitations
- the system is currently only able to support 300 files at maximum (this is because currently one 100 blocks are allocated for directory entries, and each block contains only 3 directory entries)
- the system does not allow for filenames greater than 256 characters, and each MUST NOT start with a null character and MUST end in a null character. [This also effectively means the name must be at least 2 characters long, including the null character that terminates the string]