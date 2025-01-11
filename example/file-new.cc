//
// Created by dingjing on 1/11/25.
//
#include "backup.h"

int main (int argc, char* argv[])
{
    Backup a("////////a/b/c/d/e/f/g/");
    a.test();

    Backup b("/tmp/file.b");
    b.test();

    return 0;
}
