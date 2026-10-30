/* SPDX-License-Identifier: MPL-2.0 */
/* Private test preload: model virtual MMIO mappings, not extra physical RAM. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

__attribute__((constructor)) static void reserve_device_mapping(void) {
    const char *enabled=getenv("LILSMOKE_PRIVATE_HARDWARE_QA");
    const char *value=getenv("LILSMOKE_QA_MMIO_MIB");
    if (!enabled || strcmp(enabled,"1") || !value) return;
    if (strcmp(value,"0") && strcmp(value,"24")) _exit(77);
    const size_t bytes=(size_t)atoi(value)*1024U*1024U;
    if (!bytes) return;
    void *mapping=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (mapping==MAP_FAILED) { perror("QA_MMIO mapping");_exit(78); }
    fprintf(stderr,"QA_MMIO mapped_bytes=%zu physical_pages_untouched=1\n",bytes);
}
