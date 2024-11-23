// THIS and FILESYSTEM.C ARE THE ONLY FILES WE ARE EDITING

//
// Filesystem initialized for LSU 4103 filesystem assignment
//

#include <stdio.h>
#include "filesystem.h"
#include "softwaredisk.h"

int main(int argc, char* argv[]){
    printf("Checking data structure sizes and alignments ... \n");
    if (! check_structure_alignment()){
        printf("Check failed. Filesystem not initialized and should not be used.\n");
    }
    else{
        printf("Check succeeded.\n");

        printf("Initializing filesystem...\n");
        // my FS only simply requires a completely zeroed software disk
        init_software_disk();
        printf("Done.\n");
    }
    return 0;
}