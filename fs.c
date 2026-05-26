#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "macros.h"
#include "inode.c"

#define FS_IS_POWER_OF_TWO(x) (__builtin_popcount(x) == 1)
#define FS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define FS_MAX(a, b) ((a) > (b) ? (a) : (b))

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
    int index;
    INodeIndex inode;

    size_t position;
} FS_DescriptorInfo;
typedef int FS_Descriptor;

typedef struct FS_Path FS_Path;
struct FS_Path {
    INodeString segment;
    FS_Path *next;
};

typedef struct {
    char *str;
    size_t len;
} FS_StringView;

FS_StringView fscli_str(char *s) {
    return (FS_StringView){
        s,
        strlen(s),
    };
}

INodeString fscli_strinode(FS_StringView sv) {
    INodeString s = {0};
    memcpy(s.str, sv.str, sv.len);
    s.len = sv.len;
    return s;
}

typedef struct {
    char *data;

    FS_Header header;
    FS_BlockAvailability *blockAvailability;
    INode *inodes;
    
    FS_Path *cwd;

#define FS_DESCRIPTORS 10
    FS_DescriptorInfo descriptors[FS_DESCRIPTORS];
} Filesystem;




INode *fs_getINode(Filesystem *fs, INodeIndex index) {
    if(index >= fs->header.count_inodes) return null;
    return &fs->inodes[index];
}

INode *fs_root(Filesystem *fs) {
    return fs_getINode(fs, FS_ROOT);
}


FS_Path *fs_pathFromINode(Filesystem *fs, INode *inode, FS_Path *tail) {
    FS_Path *segment = calloc(1, sizeof(FS_Path));

    // TODO: hardlinks and directories have the same field offset for name and parent, but this is not reliable
    segment->segment = inode->hardlink.name;
    segment->next = tail;

    INodeIndex nexti = inode->hardlink.parent;
    INode *next = fs_getINode(fs, nexti);

    if(next == inode) return segment;

    return fs_pathFromINode(fs, next, segment);
}

FS_Path *fs_pathFromString(FS_StringView sv) {
    FS_StringView segment = { .str = sv.str, .len = 0, };

    while(sv.len > 0 && sv.str[0] != '/') {
        segment.len += 1;
        sv.str += 1;
        sv.len -= 1;
    }

    // NOTE: empty segment with no '/' following, meaning no segment
    if(segment.len == 0 && sv.len == 0) return null;

    if(sv.len != 0) {
        sv.str += 1;
        sv.len -= 1;
    }

    FS_Path *psegment = calloc(1, sizeof(FS_Path));
    psegment->segment = fscli_strinode(segment);
    psegment->next = fs_pathFromString(sv);

    return psegment;
}

void fs_freePath(FS_Path *path) {
    if(path == null) return;
    FS_Path *next = path->next;
    free(path);
    fs_freePath(next);
}

void fs_appendPath(FS_Path *main, FS_Path *tail) {
    if(main->next == null) {
        main->next = tail;
        return;
    }

    fs_appendPath(main->next, tail);
}


size_t fs_groupCount(size_t amount, size_t groupSize) {
    size_t n = amount / groupSize;
    size_t r = amount % groupSize;
    if(r != 0) n += 1;
    return n;
}

INodeIndex fs_indexFromINode(Filesystem *fs, INode *inode) {
    return inode - (fs->inodes);
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
        .descriptors = {{0}},
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

bool fs_getBlockAvailability(Filesystem *fs, size_t index) {
    uint8_t bitIndex = index % 8;
    size_t byteIndex = index / 8;
    uint8_t mask = (1 << bitIndex);

    return (fs->blockAvailability[byteIndex] & mask) != 0;
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
        .descriptors = {{0}},
    };

    for(size_t i = 0; i < header->count_alwaysReservedBlocks; i++) {
        fs_setBlockAvailability(&fs, i, true);
    }

    return fs;
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

char *fs_getBlockPointer(Filesystem *fs, FS_Block block) {
    return fs->data + (block * fs->header.blockSize);
}

FS_Block fs_acquireBlock(Filesystem *fs) {
    for(size_t i = fs->header.count_alwaysReservedBlocks; i < fs->header.blockCount; i++) {
        if(!fs_getBlockAvailability(fs, i)) {
            fs_setBlockAvailability(fs, i, true);

            memset(fs_getBlockPointer(fs, i), 0x00, fs->header.blockSize);

            return i;
        }
    }

    return FS_NONE;
}

void fs_releaseBlock(Filesystem *fs, FS_Block block) {
    fs_setBlockAvailability(fs, block, false);
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

bool fs_remove_rawfile(Filesystem *fs, INode *rawfile) {
    if(rawfile->type != FS_INODE_RAWFILE) return false;

    for(size_t i = 0; i < FS_INODE_RAWFILE_BLOCKS_LEN; i++) {
        if(rawfile->rawfile.blocks[i] == FS_NONE) break;
        fs_releaseBlock(fs, rawfile->rawfile.blocks[i]);
    }

    rawfile->type = FS_INODE_UNUSED;
    return true;
}

INodeIndex fs_create_hardlink(Filesystem *fs, INodeIndex parenti, INodeString name, INodeIndex rawfile, bool isSymlink) {
    if(parenti == 0) parenti = FS_ROOT;

    INode *parent = fs_getINode(fs, parenti);
    if(parent->type != FS_INODE_DIRECTORY) return FS_NONE;

    size_t children = fs_getDirectoryChildren(parent);
    if(children >= FS_INODE_DIRECTORY_CHILDREN_LEN) return FS_NONE;

    for(size_t i = 0; i < children; i++) {
        INode *child = fs_getINode(fs, parent->directory.children[i]);
        if(0){}
        else if(child->type == FS_INODE_DIRECTORY && fs_strcmp(child->directory.name, name)) return FS_NONE;
        else if(child->type == FS_INODE_HARDLINK && fs_strcmp(child->hardlink.name, name)) return FS_NONE;
        else continue;
    }

    bool newRawfile = rawfile == FS_NONE;
    if(rawfile == FS_NONE) rawfile = fs_create_rawfile(fs);
    if(rawfile == FS_NONE) return FS_NONE;

    INodeIndex hardlink = fs_getUnusedINode(fs);
    if(hardlink == FS_NONE) {
        if(newRawfile) {
            fs_remove_rawfile(fs, fs_getINode(fs, rawfile));
        }

        return FS_NONE;
    }

    *fs_getINode(fs, hardlink) = (INode){
        .type = FS_INODE_HARDLINK,
        .hardlink = {
            .name = name,
            .parent = parenti,
            .isSymlink = isSymlink,
            .file = rawfile,
        }
    };
    fs_getINode(fs, rawfile)->rawfile.hardlinkReferences += 1;

    parent->directory.children[children] = hardlink;

    return hardlink;
}

INodeIndex fs_create_directory(Filesystem *fs, INodeIndex parenti, INodeString name) {
    if(parenti == 0) parenti = FS_ROOT;

    INode *parent = fs_getINode(fs, parenti);
    if(parent->type != FS_INODE_DIRECTORY) return FS_NONE;

    size_t children = fs_getDirectoryChildren(parent);
    if(children >= FS_INODE_DIRECTORY_CHILDREN_LEN) return FS_NONE;

    for(size_t i = 0; i < children; i++) {
        INode *child = fs_getINode(fs, parent->directory.children[i]);
        if(0){}
        else if(child->type == FS_INODE_DIRECTORY && fs_strcmp(child->directory.name, name)) return FS_NONE;
        else if(child->type == FS_INODE_HARDLINK && fs_strcmp(child->hardlink.name, name)) return FS_NONE;
        else continue;
    }

    INodeIndex directory = fs_getUnusedINode(fs);
    if(directory == FS_NONE) return FS_NONE;

    *fs_getINode(fs, directory) = (INode){
        .type = FS_INODE_DIRECTORY,
        .directory = {
            .name = name,
            .parent = parenti,
            .children = {0},
        },
    };

    parent->directory.children[children] = directory;

    return directory;
}

void fs_checkRawFile(Filesystem *fs, INode *rawfile) {
    if(rawfile->type != FS_INODE_RAWFILE) return;

    if(rawfile->rawfile.hardlinkReferences == 0 && rawfile->rawfile.openedReferences == 0) {
        fs_remove_rawfile(fs, rawfile);
        return;
    }

    if(rawfile->rawfile.openedReferences == 0) {
        size_t blockCount = fs_groupCount(rawfile->rawfile.size, fs->header.blockSize);
        for(size_t i = blockCount; i < FS_INODE_RAWFILE_BLOCKS_LEN; i++) {
            if(rawfile->rawfile.blocks[i] == FS_NONE) break;
            fs_releaseBlock(fs, rawfile->rawfile.blocks[i]);
            rawfile->rawfile.blocks[i] = FS_NONE;
        }
    }
}

bool fs_remove_hardlink(Filesystem *fs, INode *hardlink) {
    if(hardlink->type != FS_INODE_HARDLINK) return false;

    bool found = false;
    INode *parent = fs_getINode(fs, hardlink->hardlink.parent);
    for(size_t i = 0; i < FS_INODE_DIRECTORY_CHILDREN_LEN; i++) {
        if(fs_getINode(fs, parent->directory.children[i]) == hardlink) {
            found = true;
            parent->directory.children[i] = FS_NONE;
        }

        if(!found) continue;

        if(i < FS_INODE_DIRECTORY_CHILDREN_LEN - 1) {
            parent->directory.children[i] = parent->directory.children[i + 1];
        }
        else {
            parent->directory.children[i] = FS_NONE;
        }
    }

    hardlink->type = FS_INODE_UNUSED;
    INode *file = fs_getINode(fs, hardlink->hardlink.file);
    file->rawfile.hardlinkReferences -= 1;
    fs_checkRawFile(fs, file);

    return found;
}

bool fs_remove_directory(Filesystem *fs, INode *directory) {
    if(directory->type != FS_INODE_DIRECTORY) return false;

    for(size_t i = 0; i < FS_INODE_DIRECTORY_CHILDREN_LEN; i++) {
        if(directory->directory.children[i] == FS_NONE) break;

        INode *inode = fs_getINode(fs, directory->directory.children[i]);
        if(0) {}
        else if(inode->type == FS_INODE_DIRECTORY) {
            fs_remove_directory(fs, inode);
        }
        else if(inode->type == FS_INODE_HARDLINK) {
            fs_remove_hardlink(fs, inode);
        }
        else {
            printf("idk\n");
        }
    }

    bool found = false;
    INode *parent = fs_getINode(fs, directory->directory.parent);
    for(size_t i = 0; i < FS_INODE_DIRECTORY_CHILDREN_LEN; i++) {
        if(fs_getINode(fs, parent->directory.children[i]) == directory) {
            found = true;
            parent->directory.children[i] = (i == FS_INODE_DIRECTORY_CHILDREN_LEN - 1) ? FS_NONE : parent->directory.children[i + 1];
        }
    }

    directory->type = FS_INODE_UNUSED;

    return found;
}



size_t fs_write(Filesystem *fs, INode *inode, uint8_t *buffer, size_t len, size_t offset) {
    if(inode->type != FS_INODE_RAWFILE) return SIZE_MAX;

    size_t blockOffset = offset / fs->header.blockSize;
    size_t firstBlockOffset = offset % fs->header.blockSize;
    if(blockOffset >= FS_INODE_RAWFILE_BLOCKS_LEN) return SIZE_MAX;

    for(size_t i = 0; i < blockOffset; i++) {
        if(inode->rawfile.blocks[i] == FS_NONE) {
            FS_Block new = fs_acquireBlock(fs);
            if(new == FS_NONE) return SIZE_MAX;

            inode->rawfile.blocks[i] = new;
        }
    }

    size_t pos = 0;

    do {
        if(inode->rawfile.blocks[blockOffset] == FS_NONE) {
            FS_Block new = fs_acquireBlock(fs);
            if(new == FS_NONE) return pos;

            inode->rawfile.blocks[blockOffset] = new;
        }

        size_t chunkLength = FS_MIN(fs->header.blockSize - firstBlockOffset, len);
        memcpy(fs_getBlockPointer(fs, inode->rawfile.blocks[blockOffset]) + firstBlockOffset, buffer + pos, chunkLength);
        firstBlockOffset = 0;

        pos += chunkLength;
        len -= chunkLength;
        offset += chunkLength;

        inode->rawfile.size = FS_MAX(inode->rawfile.size, offset);

        blockOffset += 1;
    } while(len > 0 && blockOffset < FS_INODE_RAWFILE_BLOCKS_LEN);

    return pos;
}

size_t fs_read(Filesystem *fs, INode *inode, uint8_t *buffer, size_t len, size_t offset) {
    if(inode->type != FS_INODE_RAWFILE) return SIZE_MAX;

    if(offset >= inode->rawfile.size) return 0;

    size_t blockOffset = offset / fs->header.blockSize;
    size_t firstBlockOffset = offset % fs->header.blockSize;

    // NOTE: never happens due to check above
    // if(blockOffset >= FS_INODE_RAWFILE_BLOCKS_LEN) return false;

    size_t pos = 0;
    do {
        if(inode->rawfile.blocks[blockOffset] == FS_NONE) break;

        size_t chunkLength = FS_MIN(fs->header.blockSize - firstBlockOffset, len);
        memcpy(buffer + pos, fs_getBlockPointer(fs, inode->rawfile.blocks[blockOffset]) + firstBlockOffset, chunkLength);
        firstBlockOffset = 0;

        pos += chunkLength;
        len -= chunkLength;
        offset += chunkLength;

        blockOffset += 1;
    } while(len > 0 && blockOffset < FS_INODE_RAWFILE_BLOCKS_LEN);

    return pos;
}




FS_Descriptor fs_open(Filesystem *fs, INodeIndex index) {
    INode *inode = fs_getINode(fs, index);
    if(inode->type != FS_INODE_RAWFILE) return FS_NONE;

    size_t i;
    for(i = 0; i < FS_DESCRIPTORS; i++) {
        if(fs->descriptors[i].index == FS_NONE) break;
    }

    if(i == FS_DESCRIPTORS) return FS_NONE;

    fs->descriptors[i] = (FS_DescriptorInfo){
        .index = (i + 1),
        .inode = index,
        .position = 0,
    };
    inode->rawfile.openedReferences += 1;

    return (i + 1);
}

bool fs_close(Filesystem *fs, FS_Descriptor fd) {
    FS_DescriptorInfo di = fs->descriptors[fd - 1];
    if(di.index == FS_NONE) return false;

    INode *inode = fs_getINode(fs, di.inode);
    inode->rawfile.openedReferences -= 1;
    fs->descriptors[fd - 1].index = FS_NONE;

    fs_checkRawFile(fs, inode);

    return true;
}

bool fs_seek(Filesystem *fs, FS_Descriptor fd, size_t pos) {
    FS_DescriptorInfo *di = &fs->descriptors[fd - 1];
    if(di->index == FS_NONE) return false;

    di->position = pos;
    return true;
}

size_t fs_pos(Filesystem *fs, FS_Descriptor fd) {
    FS_DescriptorInfo *di = &fs->descriptors[fd - 1];
    if(di->index == FS_NONE) return SIZE_MAX;
    return di->position;
}

INode *fs_fdinode(Filesystem *fs, FS_Descriptor fd) {
    FS_DescriptorInfo *di = &fs->descriptors[fd - 1];
    if(di->index == FS_NONE) return null;
    return fs_getINode(fs, di->inode);
}

void fs_truncate(Filesystem *fs, INode *inode, size_t newSize) {
    if(inode->type != FS_INODE_RAWFILE) return;

    if(newSize == inode->rawfile.size) return;

    if(newSize > inode->rawfile.size) {
        uint8_t temp = 0;
        fs_write(fs, inode, &temp, 0, newSize);
    }
    else {
        inode->rawfile.size = newSize;
        fs_checkRawFile(fs, inode);
    }
}


INode *fs_locateInDirectory(Filesystem *fs, INode *directory, INodeString name) {
    for(int i = 0; i < FS_INODE_DIRECTORY_CHILDREN_LEN; i++) {
        INodeIndex index = directory->directory.children[i];
        if(index == 0) return null;

        INode *child = &fs->inodes[index];
        
        if(0){}
        else if(child->type == FS_INODE_HARDLINK) {
            if(fs_strcmp(name, child->hardlink.name)) return child;
        }
        else if(child->type == FS_INODE_DIRECTORY) {
            if(fs_strcmp(name, child->directory.name)) return child;
        }
        else {
            continue;
        }
    }

    return null;
}

INode *fs_locate(Filesystem *fs, INode *current, FS_Path *path) {
    if(current == null) current = fs_root(fs);

    // NOTE: we do NOT want to recurse if `current` is a symlink, because we want to append the path to the current
    // path so it gets freed eventually

    if(current->type != FS_INODE_DIRECTORY) return null;

    if(path == null) return current;
    if(fs_strcmp(path->segment, fs_str("")))    return fs_locate(fs, fs_root(fs), path->next);
    if(fs_strcmp(path->segment, fs_str(".")))   return fs_locate(fs, current, path->next);
    if(fs_strcmp(path->segment, fs_str("..")))  return fs_locate(fs, fs_getINode(fs, current->directory.parent), path->next);

    INode *next = fs_locateInDirectory(fs, current, path->segment);
    if(next == null) return null;

    if(next->type == FS_INODE_DIRECTORY) return fs_locate(fs, next, path->next);
    if(next->type == FS_INODE_HARDLINK && !next->hardlink.isSymlink) {
        if(path->next != null) return null;
        return next;
    }

    if(next->type == FS_INODE_HARDLINK && next->hardlink.isSymlink) {
        INode *file = fs_getINode(fs, next->hardlink.file);
        uint8_t *buffer = calloc(file->rawfile.size, sizeof(uint8_t));
        fs_read(fs, file, buffer, file->rawfile.size, 0);
        FS_Path *spath = fs_pathFromString((FS_StringView){ (char *)buffer, file->rawfile.size });
        free(buffer);

        fs_appendPath(spath, path->next);
        path->next = spath;

        return fs_locate(fs, current, spath);
    }

    return null;
}
