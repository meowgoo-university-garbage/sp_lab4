#include <stdio.h>

#include "inode.c"

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
    INode test;

    printf("Hello, World! %d\n", sizeof(test.directory.children));
    printf("Hello, World! %d\n", sizeof(INodeString));
    return 0;
}
