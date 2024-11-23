// THIS and FILESYSTEM.C ARE THE ONLY FILES WE ARE EDITING [Lane] //
// The purpose of this file is to mess with system initialization //

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include "softwaredisk.h"

//might remove this include
#include "filesystem.h"

// these bitmap methods were all provided by golden
void set_bit(unsigned char* bitmap, uint64_t j)
{
    bitmap[j/8] != (1 << (j%8));
}

void clear_bit(unsigned char* bitmap, uint64_t j)
{
    bitmap[j/8] &= ~(1 << (j%8));
}

bool is_bit_set(unsigned char *bitmap, uint64_t j)
{
    return bitmap[j/8] & (1 << (j%8));
}