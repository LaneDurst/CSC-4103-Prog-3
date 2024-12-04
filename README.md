# Design Document
This is the Design Document for Programming Assignment #3 of CSC 4103, done by Lane Durst & Connor Morris in the Fall 2024 semester.

## Diagram
```
+------------------------------------------------------------------------+
| DATA BITMAP | INODE BITMAP | ======== | XXXX | ~~~~~ DATA BLOCKS ~~~~~ |
+------------------------------------------------------------------------+
```
Our filesystem formats the software disk into into 8,192 blocks of size 1024 bytes each. The blocks taken up by our filesystem are mapped as follows:
- The first two blocks are taken up by the Data Bitmap & Inode Bitmap, respectively. The explanation for the Bitmap Structure is located in another section of this document.
- The next 256 blocks are taken up by Inode Blocks, which store 32 Inodes per block.
- After the Inode Blocks, there are 150 Directory Entry Blocks, each filled with 2 Directory Entries. The directory entries store the name of a file and a Boolean value that keeps track of whether the file for that directory entry is currently open.
- All of the remaining blocks in the filesystem are for storing data; the specific number is 7,782 Data Blocks. We allow for a maximum of 300 files in our filesystem implementation, meaning that this number of free blocks is perfect for our usage.

## Block Allocation (Simplified)
- Block 0 is allocated for the Data Bitmap.
- Block 1 is allocated for the Inode Bitmap.
- Blocks 2 - 258 are allocated for Inode Blocks.
- Blocks 259 - 409 are allocated for Directory Entry Blocks.
- Blocks 410 - 8192 are allocated for Data Storage Blocks.

## Bitmap Structure
There are two Bitmaps contained in this filesystem.c implementation, the Data Bitmap and the Inode Nitmap. Their purpose is to log which Data Blocks and which Inodes are free to be altered within the software disk. The bitmap structure is composed of an array of uint8_t elements, and is equal in size to an entire software disk block. In this case, that means 1024 uint8_t elements. However, these elements are not what operations on the bitmap affect directly. Instead, each inode or data block represented by a bitmap is represented by each individual bit of these elemenets. This means, for instance, that inode number 12 is not represented by the 12th element in the array, but is instead the 4th bit of the 2nd element of the array.

## Directory Entry Strucutre
The Directory Entry Structure is composed of two parts, a reference to the file, and a bool stating if the file is 'open'. In this case, the refrence is the name associated with the file. We do not store the Inode number in the directory entry as the directory entry number and the Inode number are always equivalent in our implementation.

### Note on Directory Blocks
Each directory block in our implementation contains 2 directory entries, each of size 512. No other data is contained in a directory block, apart from these entries.

## Implementation limitations
- The system is currently only able to support 300 files at maximum. This is due to only 150 blocks being allocated for directory entries, and only 2 directory entries fit in each directory block.
- The system does not allow for filenames greater than 256 characters, and each MUST NOT start with a null character and MUST end in a null character. This also effectively means a filename must be at least 2 characters long, including the null character that terminates the string.