/* SPDX-License-Identifier: MPL-2.0 */
/* Asset-free replacement for hexdump -v -e '1/1 "0x%X,"'; echo.
 * Default decimal tokens have identical int values in C initializers, with
 * less output. --hex preserves the original uppercase, unpadded hex tokens.
 * Both modes append exactly one newline, even for an empty input file.
 * Fixed storage, no per-byte printf, no mmap or input-sized allocation.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define BLOCK 16384
static unsigned char input[BLOCK];
static char output[BLOCK * 5];
static char tokens[256][5];
static unsigned char lengths[256];

static int failure(const char *operation, const char *path)
{
    fprintf(stderr, "bin2c: %s '%s': %s\n", operation, path,
            strerror(errno ? errno : EIO));
    return 1;
}

int main(int argc, char **argv)
{
    static const char hex[] = "0123456789ABCDEF";
    const char *path;
    FILE *source;
    size_t count, i, used;
    unsigned int value;
    int arg = 1, hex_mode = 0;

    if (arg < argc && strcmp(argv[arg], "--hex") == 0) {
        hex_mode = 1;
        ++arg;
    }
    if (arg < argc && strcmp(argv[arg], "--") == 0)
        ++arg;
    else if (arg < argc && argv[arg][0] == '-')
        goto usage;
    if (arg + 1 != argc)
        goto usage;
    path = argv[arg];
#ifdef SIGPIPE
    /* Report broken pipes as ordinary write errors, including under QEMU. */
    signal(SIGPIPE, SIG_IGN);
#endif
    source = fopen(path, "rb");
    if (!source)
        return failure("cannot open", path);
    for (value = 0; value < 256; ++value) {
        used = 0;
        if (hex_mode) {
            tokens[value][used++] = '0';
            tokens[value][used++] = 'x';
            if (value >= 16)
                tokens[value][used++] = hex[value >> 4];
            tokens[value][used++] = hex[value & 15];
        } else {
            if (value >= 100)
                tokens[value][used++] = (char)('0' + value / 100);
            if (value >= 10)
                tokens[value][used++] = (char)('0' + (value / 10) % 10);
            tokens[value][used++] = (char)('0' + value % 10);
        }
        tokens[value][used++] = ',';
        lengths[value] = (unsigned char)used;
    }
    while ((count = fread(input, 1, sizeof(input), source)) != 0) {
        used = 0;
        for (i = 0; i < count; ++i) {
            value = input[i];
            memcpy(output + used, tokens[value], lengths[value]);
            used += lengths[value];
        }
        if (fwrite(output, 1, used, stdout) != used) {
            int status = failure("cannot write", "stdout");
            fclose(source);
            return status;
        }
    }
    if (ferror(source)) {
        int status = failure("cannot read", path);
        fclose(source);
        return status;
    }
    if (fclose(source) != 0)
        return failure("cannot close", path);
    if (fputc('\n', stdout) == EOF || fflush(stdout) != 0)
        return failure("cannot write", "stdout");
    if (fclose(stdout) != 0)
        return failure("cannot close", "stdout");
    return 0;

usage:
    fprintf(stderr, "Usage: bin2c [--hex] [--] INPUT\n");
    return 2;
}
