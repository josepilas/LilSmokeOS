#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t now_us(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000000U + t.tv_nsec / 1000U;
}
int main(void) {
    volatile uint32_t integer = 7;
    volatile float single = 0.5f;
    volatile double wide = 0.5;
    const unsigned loops = 16384;
    uint64_t start = now_us();
    for (unsigned i = 0; i < loops; ++i) integer = integer * 1664525U + 1013904223U;
    printf("PROBE integer_us=%llu value=%u\n", (unsigned long long)(now_us()-start), integer);
    start = now_us();
    for (unsigned i = 0; i < loops; ++i) single = single * 0.999f + 0.0005f;
    uint32_t bits; float f = single; memcpy(&bits, &f, sizeof bits);
    printf("PROBE single_us=%llu bits=%08x\n", (unsigned long long)(now_us()-start), bits);
    start = now_us();
    for (unsigned i = 0; i < loops; ++i) wide = wide * 0.999 + 0.0005;
    uint64_t dbits; double d = wide; memcpy(&dbits, &d, sizeof dbits);
    printf("PROBE double_us=%llu bits=%016llx\n", (unsigned long long)(now_us()-start), (unsigned long long)dbits);
    start = now_us();
    for (unsigned i = 0; i < 512; ++i) wide = sin(wide) * 0.5 + 0.5;
    d = wide; memcpy(&dbits, &d, sizeof dbits);
    printf("PROBE trig_us=%llu bits=%016llx\n", (unsigned long long)(now_us()-start), (unsigned long long)dbits);
    puts("C42_FPU_PROBE_OK");
    return 0;
}
