## Design Document
Lane Durst & Connor Morris

### Bitmap Blocks
        The first two blocks of the software disk contain bitmaps for the Inodes and Data.
    Block 0 is the Data Bitmap Block, while Block 1 is the Inode Bitmap Block.

### Inode Blocks
        Each Inode contains a 4 byte size variable as well as an array of 14, 2 byte b[?] values.
    This causes each Inode to be 32 bytes in size which allows for 32 Inodes total to cleanly fit
    into a 1024 byte Inode Block.

        Further, there are a total of 14[?] inode blocks included in the software disk. 13 are for
    Direct Inode Blocks, and the 14th is for the single Indirect Inode Block. 

### Directory Blocks

## Directory Structure

## Implementation Limits