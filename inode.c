#include <stdint.h>

typedef uint8_t INodeType;
#define FS_INODE_UNUSED 0
#define FS_INODE_DIRECTORY 1
#define FS_INODE_HARDLINK 2
#define FS_INODE_RAWFILE 3

typedef uint16_t INodeIndex;

#define FS_MAX_PATH_SEGMENT (64 - 1)

// NOTE: null-terminated strings are not allowed in this household
typedef struct {
    char    str[FS_MAX_PATH_SEGMENT];
    uint8_t len;
} INodeString;

// TODO: for now 30 will be the limit for children/blocks, I'll expand on that
// in lab5 for content

typedef struct {
    INodeType type;

    union {
        struct {
            INodeString name;

            // children data
#define FS_INODE_DIRECTORY_CHILDREN_LEN 30
            INodeIndex children[FS_INODE_DIRECTORY_CHILDREN_LEN];
            // INodeIndex continuation;
        } directory;
        struct {
            INodeString name;
            INodeIndex  file;
        } hardlink;
        struct {
            uint8_t hardlinkReferences;
            uint8_t openedReferences;

            // block data
#define FS_INODE_RAWFILE_BLOCKS_LEN 30
            uint32_t blocks[FS_INODE_RAWFILE_BLOCKS_LEN];
            // INodeIndex continuation;
        } rawfile;
    };
} INode;
