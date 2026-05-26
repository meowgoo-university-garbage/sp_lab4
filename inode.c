#include <stdint.h>
#include <string.h>

typedef uint8_t INodeType;
#define FS_INODE_UNUSED 0
#define FS_INODE_DIRECTORY 1
#define FS_INODE_HARDLINK 2
#define FS_INODE_RAWFILE 3


typedef uint32_t FS_Block;

typedef uint16_t INodeIndex;
// NOTE: index reuse because it is always clear which one is implied
#define FS_ROOT 0
#define FS_NONE 0

#define FS_MAX_PATH_SEGMENT (64 - 1)

// NOTE: null-terminated strings are not allowed in this household
typedef struct {
    char    str[FS_MAX_PATH_SEGMENT];
    uint8_t len;
} INodeString;

// TODO: for now some constant will be the limit for children/blocks, I'll probably expand on that
// in lab5 for content

typedef struct {
    INodeType type;

    union {
        struct {
            INodeString name;
            INodeIndex  parent;

            // children data
#define FS_INODE_DIRECTORY_CHILDREN_LEN 26
            INodeIndex  children[FS_INODE_DIRECTORY_CHILDREN_LEN];
            // INodeIndex continuation;
        } directory;
        struct {
            INodeString name;
            INodeIndex  parent;
            INodeIndex  file;
        } hardlink;
        struct {
            uint8_t hardlinkReferences;
            uint8_t openedReferences;

            size_t size;

            // block data
#define FS_INODE_RAWFILE_BLOCKS_LEN 26
            FS_Block blocks[FS_INODE_RAWFILE_BLOCKS_LEN];
            // INodeIndex continuation;
        } rawfile;
    };
} INode;

bool fs_strcmp(INodeString a, INodeString b) {
    if(a.len != b.len) return false;
    for(int i = 0; i < a.len; i++) {
        if(a.str[i] != b.str[i]) return false;
    }

    return true;
}

INodeString fs_str(char *s) {
    // TODO: reading the man page, im not sure if this can make a max length string
    size_t len = strnlen(s, FS_MAX_PATH_SEGMENT);
    INodeString str;
    memcpy(&str.str, s, len);
    str.len = len;
    return str;
}
