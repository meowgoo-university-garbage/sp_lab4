#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fs.c"

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

FS_StringView fscli_popWord(FS_StringView *line) {
    while(line->len > 0 && line->str[0] == ' ') {
        line->str += 1;
        line->len -= 1;
    }

    if(line->len == 0) return *line;

    FS_StringView word = *line;
    word.len = 0;

    while(line->len > 0 && line->str[0] != ' ') {
        word.len += 1;
        line->str += 1;
        line->len -= 1;
    }

    while(line->len > 0 && line->str[0] == ' ') {
        line->str += 1;
        line->len -= 1;
    }

    return word;
}

bool fscli_strcmp(FS_StringView a, FS_StringView b) {
    if(a.len != b.len) return false;
    for(size_t i = 0; i < a.len; i++) {
        if(a.str[i] != b.str[i]) return false;
    }

    return true;
}

FS_StringView fscli_getline() {
    FS_StringView sv = {0};
    getline(&sv.str, &sv.len, stdin);
    sv.len = strlen(sv.str);
    if(sv.len > 0) sv.len -= 1;
    return sv;
}

size_t fscli_parseInt(FS_StringView sv) {
    size_t acc = 0;
    for(size_t i = 0; i < sv.len; i++) {
        int digit = (int)sv.str[i] - '0';
        if(digit < 0 || digit > 9) return -1;

        acc = acc * 10 + digit;
    }
    
    return acc;
}

bool fscli_iteration(Filesystem *fs) {
    printf("> ");
    fflush(stdout);

    FS_StringView line = fscli_getline();
    FS_StringView command = fscli_popWord(&line);

    if(command.len == 0) {
        free(line.str);
        return true;
    }
#define COMMAND(x) else if(fscli_strcmp(command, fscli_str(x)))

#define POP_FILE_NAME(name)  \
    FS_StringView name = fscli_popWord(&line); \
    if(name.len == 0) { \
        printf("No name provided\n"); \
        return true; \
    } \
 \
    if(name.len > FS_MAX_PATH_SEGMENT) { \
        printf("Max file name: %d\n", FS_MAX_PATH_SEGMENT); \
        return true; \
    }

#define POP_FD(fd) \
    FS_StringView fds = fscli_popWord(&line); \
    if(fds.len != 1) { \
        printf("Provide a file descriptor 0 - 9\n"); \
        return true; \
    } \
 \
    FS_Descriptor fd = fds.str[0] - '0' + 1; \
    if(fd < 1 || fd > 10) { \
        printf("Provide a file descriptor 0 - 9\n"); \
        return true; \
    }

#define POP_INT(i) \
    FS_StringView is = fscli_popWord(&line); \
    if(is.len == 0) { \
        printf("Provide an integer\n"); \
        return true; \
    } \
 \
    size_t i = fscli_parseInt(is); \
    if(i == -1) { \
        printf("Provide an integer\n"); \
        return true; \
    }

    COMMAND("quit") {
        printf("Quitting\n");
        return false;
    }
    COMMAND("q") {
        printf("Quitting\n");
        return false;
    }
    COMMAND("mkfs") {
        if(fs->inodes != null) {
            printf("The file system has already been initialized\n");
        }
        else {
            size_t blockSize = 4096;
            size_t blockCount = 16;
            size_t inodeCount = 32;

            char *buffer = malloc(blockSize * blockCount);
            *fs = fs_initialize(buffer, blockSize * blockCount, blockSize, inodeCount);
            printf("Initialized the file system\n");
        }
    }
    else if(fs->inodes == null) {
        printf("File system has not been initialized\n");
        return true;
    }
    COMMAND("touch") {
        POP_FILE_NAME(name);

        INodeIndex hardlink = fs_create_hardlink(fs, FS_ROOT, fscli_strinode(name), FS_NONE);
        if(hardlink == FS_NONE) {
            printf("Couldn't create the file\n");
        }
        else {
            printf("File created\n");
        }
    }
    COMMAND("stat") {
        POP_FILE_NAME(name);

        INodeIndex index = fs_locate(fs, null, fscli_strinode(name));
        if(index == FS_NONE) {
            printf("File not found\n");
            return true;
        }

        INode *inode = fs_getINode(fs, index);

        if(0) {}
        else if(inode->type == FS_INODE_HARDLINK) {
            INode *file = fs_getINode(fs, inode->hardlink.file);

            printf("Name: \"%.*s\"\n", inode->hardlink.name.len, inode->hardlink.name.str);
            printf("Size: %ld\n", file->rawfile.size);
            printf("Hardlink INode: %d\n", index);
            printf("Rawfile  INode: %d\n", inode->hardlink.file);
        }
        else {
            printf("idk\n");
        }
    }
    COMMAND("ls") {
        INode *dir = fs_getINode(fs, FS_ROOT);

        bool printedAny = false;
        for(size_t i = 0; i < FS_INODE_DIRECTORY_CHILDREN_LEN; i++) {
            if(dir->directory.children[i] == FS_NONE) break;
            INode *child = fs_getINode(fs, dir->directory.children[i]);

            if(0) {}
            else if(child->type == FS_INODE_DIRECTORY) {
                printf("/%.*s/\n", child->directory.name.len, child->directory.name.str);
            }
            else if(child->type == FS_INODE_HARDLINK) {
                printf("/%.*s\n", child->hardlink.name.len, child->hardlink.name.str);
            }
            else {
                printf("idk\n");
            }

            printedAny = true;
        }

        if(!printedAny) {
            printf("Current directory is empty\n");
        }
    }
    COMMAND("open") {
        POP_FILE_NAME(name);

        INodeIndex index = fs_locate(fs, null, fscli_strinode(name));
        if(index == FS_NONE) {
            printf("File not found\n");
            return true;
        }
        INode *inode = fs_getINode(fs, index);
        if(inode->type != FS_INODE_HARDLINK) {
            printf("Only hardlinks can be opened\n");
            return true;
        }

        FS_Descriptor fd = fs_open(fs, inode->hardlink.file);
        if(fd == 0) {
            printf("Couldn't open the file\n");
            return true;
        }

        printf("File is bound to file descriptor %d\n", fd - 1);
    }
    COMMAND("close") {
        POP_FD(fd);

        bool result = fs_close(fs, fd);
        if(result) {
            printf("File descriptor closed\n");
        }
        else {
            printf("Couldn't close file descriptor\n");
        }
    }
    COMMAND("seek") {
        POP_FD(fd);
        POP_INT(pos);

        bool result = fs_seek(fs, fd, pos);
        if(result) {
            printf("Moved to specified position\n");
        }
        else {
            printf("Failed to seek\n");
        }
    }
    COMMAND("read") {
        POP_FD(fd);
        POP_INT(len);

        size_t offset = fs_pos(fs, fd);
        if(offset == -1) {
            printf("Failed to read\n");
        }

        INode *inode = fs_fdinode(fs, fd);
        if(inode == null) {
            printf("Failed to read\n");
        }

        uint8_t *buffer = calloc(len, sizeof(uint8_t));
        size_t amount = fs_read(fs, inode, buffer, len, offset);
        fs_seek(fs, fd, offset + amount);

        if(amount != len) {
            printf("Read %ld instead of %ld bytes\n", amount, len);
        }

        printf("Read data: \"");
        fflush(stdout);
        write(STDOUT_FILENO, buffer, amount);
        printf("\"\n");
    }
    COMMAND("write") {
        POP_FD(fd);

        size_t offset = fs_pos(fs, fd);
        if(offset == -1) {
            printf("Failed to write\n");
        }

        INode *inode = fs_fdinode(fs, fd);
        if(inode == null) {
            printf("Failed to write\n");
        }

        size_t amount = fs_write(fs, inode, line.str, line.len, offset);
        fs_seek(fs, fd, offset + amount);

        if(amount != line.len) {
            printf("Wrote %ld instead of %ld bytes\n", amount, line.len);
        }
    }
    COMMAND("ln") {
        POP_FILE_NAME(originalName);
        POP_FILE_NAME(copyName);

        INodeIndex index = fs_locate(fs, null, fscli_strinode(originalName));
        if(index == FS_NONE) {
            printf("File not found\n");
            return true;
        }
        INode *inode = fs_getINode(fs, index);
        if(inode->type != FS_INODE_HARDLINK) {
            printf("Only hardlinks can be linked\n");
            return true;
        }

        INodeIndex copy = fs_create_hardlink(fs, FS_ROOT, fscli_strinode(copyName), inode->hardlink.file);
        if(copy == FS_NONE) {
            printf("Failed to link\n");
            return true;
        }

        printf("File linked\n");
    }
    COMMAND("rm") {
        POP_FILE_NAME(name);

        INodeIndex index = fs_locate(fs, null, fscli_strinode(name));
        if(index == FS_NONE) {
            printf("File not found\n");
            return true;
        }
        INode *inode = fs_getINode(fs, index);
        if(inode->type != FS_INODE_HARDLINK) {
            printf("Only hardlinks can be removed\n");
            return true;
        }

        bool result = fs_remove_hardlink(fs, index);
        if(result) {
            printf("Removed file\n");
        }
        else {
            printf("Failed to remove file\n");
        }
    }
    COMMAND("inodes") {
        for(size_t i = 0; i < fs->header.count_inodes; i++) {
            INode *inode = &fs->inodes[i];

            if(0) {}
            else if(inode->type == FS_INODE_UNUSED) {
                printf("%3d | UNUSED\n", i);
            }
            else if(inode->type == FS_INODE_HARDLINK) {
                printf("%3d | HARDLINK \"%.*s\" to %d\n", i, inode->hardlink.name.len, inode->hardlink.name.str, inode->hardlink.file);
            }
            else if(inode->type == FS_INODE_RAWFILE) {
                printf("%3d | RAWFILE with size %ld\n", i, inode->rawfile.size);
            }
            else if(inode->type == FS_INODE_DIRECTORY) {
                printf("%3d | DIRECTORY \"%.*s\"\n", i, inode->directory.name.len, inode->directory.name.str);
            }
        }
    }
    COMMAND("truncate") {
        POP_FILE_NAME(name);
        POP_INT(len);

        INodeIndex index = fs_locate(fs, null, fscli_strinode(name));
        if(index == FS_NONE) {
            printf("File not found\n");
            return true;
        }
        INode *inode = fs_getINode(fs, index);
        if(inode->type != FS_INODE_HARDLINK) {
            printf("Only files can be truncated\n");
            return true;
        }

        fs_truncate(fs, fs_getINode(fs, inode->hardlink.file), len);

        printf("New size: %ld\n", fs_getINode(fs, inode->hardlink.file)->rawfile.size);
    }
    COMMAND("blocks") {
        printf("[ |");
        for(size_t i = 0; i < fs->header.count_alwaysReservedBlocks; i++) {
            printf("-");
        }

        for(size_t i = fs->header.count_alwaysReservedBlocks; i < fs->header.blockCount; i++) {
            bool b = fs_getBlockAvailability(fs, i);
            if(b) {
                printf("#");
            }
            else {
                printf(" ");
            }
        }

        printf("| ]\n");
    }
#undef COMMAND

    return true;
}
