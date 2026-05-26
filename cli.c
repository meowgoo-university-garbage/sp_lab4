#include <stdio.h>
#include <stdlib.h>

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
        FS_StringView name = fscli_popWord(&line);
        if(name.len == 0) {
            printf("No name provided\n");
            return true;
        }

        if(name.len > FS_MAX_PATH_SEGMENT) {
            printf("Max file name: %d\n", FS_MAX_PATH_SEGMENT);
            return true;
        }

        INodeIndex hardlink = fs_create_hardlink(fs, FS_ROOT, fscli_strinode(name), FS_NONE);
        if(hardlink == FS_NONE) {
            printf("Couldn't create the file\n");
        }
        else {
            printf("File created\n");
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
#undef COMMAND

    return true;
}
