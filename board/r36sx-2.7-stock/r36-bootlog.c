/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#if !defined(__mips__)
#define R36_BOOTLOG_HOST
#endif
#include "r36-bootlog.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc==2) return r36_boot_event(argv[1])<0?1:0;
    if(argc==4 && !strcmp(argv[1],"--journal")) {
        int result=r36_journal_write(argv[2],argv[3]);
        if(result<0) fprintf(stderr,"checkpoint failed (%d)\n",result);
        return result<0?1:0;
    }
    fputs("usage: r36-bootlog EVENT | --journal EXISTING-FILE EVENT\n",stderr);
    return 2;
}
