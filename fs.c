#include <stdint.h>

#include "inode.c"

typedef struct {
    size_t len;
    size_t blockSize;
    size_t blockCount;

    size_t offset_blockAvailability;
    size_t offset_inodes;

    // TODO: im kinda thinking that we could make inode array
    // expandable by making it a segmented linked list or smth
    size_t count_inodes;
} FS_Header;

typedef struct {
    char *data;

    FS_Header header;
} Filesystem;

// Loads a file system from a previously initialized buffer
Filesystem fs_load(char *data, size_t len) {

}

// Creates a new file system on an undefined buffer
Filesystem fs_initialize(char *data, size_t len, size_t blockSize) {

}
