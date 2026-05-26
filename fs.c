#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "macros.h"
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
typedef uint8_t *FS_Block;

typedef struct {
    int index;
    INodeIndex inode;

    size_t position;
} FS_Descriptor;

typedef struct {
    char *data;

    FS_Header header;
    FS_BlockAvailability *blockAvailability;
    INode *inodes;

    // TODO: opened files
} Filesystem;


size_t fs_groupCount(size_t amount, size_t groupSize) {
    size_t n = amount / groupSize;
    size_t r = amount % groupSize;
    if(r != 0) n += 1;
    return n;
}

// Loads a file system from a previously initialized buffer
Filesystem fs_load(char *data, size_t len) {
    assert(len >= sizeof(FS_Header));

    FS_Header *header = (FS_Header *)data;
    assert(len == header->len);

    Filesystem fs = {
        .data = data,
        .header = *header,
        .blockAvailability = (FS_BlockAvailability *)((char *)header + header->offset_blockAvailability),
        .inodes = (INode *)((char *)header + header->offset_inodes),
    };

    return fs;
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
            .name = {{0}},
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

    for(size_t i = 0; i < header->count_alwaysReservedBlocks; i++) {
        fs_setBlockAvailability(&fs, i, true);
    }

    return fs;
}

INode *fs_getINode(Filesystem *fs, INodeIndex index) {
    if(index >= fs->header.count_inodes) return null;
    return &fs->inodes[index];
}

INode *fs_root(Filesystem *fs) {
    return fs_getINode(fs, FS_ROOT);
}

INode *fs_locate(Filesystem *fs, INode *parent, INodeString path) {
    // TODO: for now we define path to be the name of a file in root directory 

    if(parent == null) parent = fs_root(fs);

    for(int i = 0; i < FS_INODE_DIRECTORY_CHILDREN_LEN; i++) {
        INodeIndex index = parent->directory.children[i];
        if(index == 0) return null;

        INode *child = &fs->inodes[index];
        
        if(0){}
        else if(child->type == FS_INODE_HARDLINK) {
            if(fs_strcmp(path, child->hardlink.name)) return child;
        }
        else if(child->type == FS_INODE_DIRECTORY) {
            if(fs_strcmp(path, child->directory.name)) return child;
        }
        else {
            continue;
        }
        
        return child;
    }

    return null;
}

size_t fs_getDirectoryChildren(INode *dir) {
    if(dir->type != FS_INODE_DIRECTORY) return 0;

    size_t children = 0;
    for(size_t i = 0; i < FS_INODE_DIRECTORY_CHILDREN_LEN; i++) {
        if(dir->directory.children[i] != FS_NONE) children += 1;
        else break;
    }

    return children;
}

INodeIndex fs_getUnusedINode(Filesystem *fs) {
    for(size_t i = 1; i < fs->header.count_inodes; i++) {
        if(fs->inodes[i].type == FS_INODE_UNUSED) return i;
    }

    return FS_NONE;
}

INodeIndex fs_create_rawfile(Filesystem *fs) {
    INodeIndex rawfile = fs_getUnusedINode(fs);
    if(rawfile == FS_NONE) return FS_NONE;

    *fs_getINode(fs, rawfile) = (INode){
        .type = FS_INODE_RAWFILE,
        .rawfile = {
            .hardlinkReferences = 0,
            .openedReferences = 0,
            .size = 0,
            .blocks = {0},
        },
    };

    return rawfile;
}

INodeIndex fs_create_hardlink(Filesystem *fs, INodeIndex parenti, INodeString name, INodeIndex rawfile) {
    if(parenti == 0) parenti = FS_ROOT;

    INode *parent = fs_getINode(fs, parenti);
    if(parent->type != FS_INODE_DIRECTORY) return FS_NONE;

    size_t children = fs_getDirectoryChildren(parent);
    if(children >= FS_INODE_DIRECTORY_CHILDREN_LEN) return FS_NONE;

    bool newRawfile = rawfile == FS_NONE;
    if(rawfile == FS_NONE) rawfile = fs_create_rawfile(fs);
    if(rawfile == FS_NONE) return FS_NONE;

    INodeIndex hardlink = fs_getUnusedINode(fs);
    if(hardlink == FS_NONE) {
        // TODO: delete rawfile if created
        newRawfile = newRawfile;
        return FS_NONE;
    }

    *fs_getINode(fs, hardlink) = (INode){
        .type = FS_INODE_HARDLINK,
        .hardlink = {
            .name = name,
            .parent = parenti,
            .file = rawfile,
        }
    };
    fs_getINode(fs, rawfile)->rawfile.hardlinkReferences += 1;

    parent->directory.children[children] = hardlink;

    return hardlink;
}
