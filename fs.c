#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "inode.c"

#define FS_IS_POWER_OF_TWO(x) (__builtin_popcount(x) == 1)

typedef struct {
    size_t len;
    size_t blockSize;
    size_t blockCount;

    size_t offset_blockAvailability;
    size_t offset_inodes;

    // TODO: im kinda thinking that we could make inode array
    // expandable by making it a segmented linked list or smth
    size_t count_inodes;

    size_t count_alwaysReservedBlocks;

    // TODO: having a checksum would be a good idea potentially
} FS_Header;

typedef uint8_t FS_BlockAvailability;

typedef struct {
    char *data;

    FS_Header header;
    FS_BlockAvailability *blockAvailability;
    INode *inodes;

    // TODO: opened files
} Filesystem;

// Loads a file system from a previously initialized buffer
Filesystem fs_load(char *data, size_t len) {

}

size_t fs_groupCount(size_t amount, size_t groupSize) {
    size_t n = amount / groupSize;
    size_t r = amount % groupSize;
    if(r != 0) n += 1;
    return n;
}

void fs_initialize_blockAvailability(FS_Header *header) {
    // NOTE: how many groups of 8 we need to describe all blocks
    size_t bytesCount = fs_groupCount(header->blockCount, sizeof(uint8_t));
    size_t blockCount = fs_groupCount(bytesCount, header->blockSize);

    assert(header->blockCount >= 1 + blockCount);
    header->offset_blockAvailability = 0 + (1 * header->blockSize);
    header->offset_inodes            = 0 + (1 * header->blockSize) + (blockCount * header->blockSize);

    memset((char *)header + header->offset_blockAvailability, 0x00, bytesCount);

    return;
}

void fs_initialize_inodes(FS_Header *header) {
    size_t blockCount = fs_groupCount(header->count_inodes, header->blockSize);
    size_t beforeCount = header->offset_inodes / header->blockSize;

    assert(header->blockCount >= beforeCount + blockCount);

    memset((char *)header + header->offset_inodes, 0x00, sizeof(INode) * header->count_inodes);

    header->count_alwaysReservedBlocks = beforeCount + blockCount;

    INode *root = (INode *)((char *)header + header->offset_inodes);
    *root = (INode){
        .type = FS_INODE_DIRECTORY,
        .directory = {
            .name = {0},
            .parent = 0,
            .children = {0},
        },
    };

    return;
}

void fs_setBlockAvailability(Filesystem *fs, size_t index, bool value) {
    uint8_t bitIndex = index % 8;
    size_t byteIndex = index / 8;
    uint8_t mask = (1 << bitIndex);

    if(value) {
        fs->blockAvailability[byteIndex] |= mask;
    }
    else {
        fs->blockAvailability[byteIndex] &= ~mask;
    }
}

// Creates a new file system on an undefined buffer
Filesystem fs_initialize(char *data, size_t len, size_t blockSize, size_t inodeCount) {
    assert(FS_IS_POWER_OF_TWO(len));
    assert(FS_IS_POWER_OF_TWO(blockSize));

    assert(len       >= sizeof(FS_Header));
    assert(blockSize >= sizeof(FS_Header));

    FS_Header *header = (FS_Header *)data;
    *header = (FS_Header){
        .len = len,
        .blockSize = blockSize,
        .blockCount = (len / blockSize), // NOTE: we have asserted that both are a power of 2
        .count_inodes = inodeCount,
    };

    fs_initialize_blockAvailability(header);
    fs_initialize_inodes(header);

    Filesystem fs = {
        .data = data,
        .header = *header,
        .blockAvailability = (FS_BlockAvailability *)((char *)header + header->offset_blockAvailability),
        .inodes = (INode *)((char *)header + header->offset_inodes),
    };

    for(int i = 0; i < header->count_alwaysReservedBlocks; i++) {
        fs_setBlockAvailability(&fs, i, true);
    }

    return fs;
}
