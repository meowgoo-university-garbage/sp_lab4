#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fs.c"

/*

okay first of all, even though this filesystem will be in-memory, i
wanna emulate having limited storage, so that's that.

im not exactly sure how to organize this entire thing, the directory
may have an unknown amount of children so we can't just store a list
of pointers. Same thing with files really - they have to index all
the blocks in order. I suppose we have a small inode, which points to
a block that stores children/block range data, which describes the 
actual blocks where the data is stored. Of course we can have an
optimization for small enough files/directories yada yada, but this
still feels wasteful. Alternatively we could have an item in the inode
array that is dedicated to storing this extra information? I'm inclined
to do this instead of wasting actual blocks for storing this tbh

*/

int main() {
    char *buffer = malloc(4096 * 16);
    Filesystem fs = fs_initialize(buffer, 4096 * 16, 4096, 16);

    INodeIndex hardlink = fs_create_hardlink(&fs, FS_ROOT, fs_str("test"), FS_NONE);
    INode *file = fs_getINode(&fs, fs_getINode(&fs, hardlink)->hardlink.file);
    bool result = fs_write(&fs, file, "my message", 10, 5);

    char mbuffer[256];
    size_t len = fs_read(&fs, file, mbuffer, 10, 0);
    mbuffer[len] = '\0';

    // printf("%d %.*s\n", result, len, mbuffer);
    write(STDOUT_FILENO, mbuffer, len);


    printf("Hello\n");

    return 0;
}
