#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    if (!strcmp(argv[2], "cancel")) return 1;
    if (!strcmp(argv[2], "error")) return 3;
    if (!strcmp(argv[2], "full")) puts("abcde");
    else if (!strcmp(argv[2], "overflow")) puts("abcdef");
    else puts("hello");
    return 0;
}
