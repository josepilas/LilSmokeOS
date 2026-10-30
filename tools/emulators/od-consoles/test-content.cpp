#include "od-content.h"

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *resolved = od_resolve_content(argv[1], "gb");
    if (!resolved) return 1;
    puts(resolved);
    return 0;
}
