// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        puts("Echoy core runner ready");
        return 0;
    }

    fprintf(stderr, "usage: %s --self-test\n", argv[0]);
    return 2;
}
