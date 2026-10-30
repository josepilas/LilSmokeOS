/* SPDX-License-Identifier: MPL-2.0. Opt-in diagnostic; never recovers a fault. */
#ifndef LILSMOKE_NATIVE_TRACE_H
#define LILSMOKE_NATIVE_TRACE_H
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
static void lilos_trace_fault(int value,siginfo_t *info,void *context)
{
    (void)info;
    ucontext_t *u=(ucontext_t *)context;
    unsigned long long words[]={u->uc_mcontext.pc,u->uc_mcontext.gregs[31],u->uc_mcontext.gregs[29]};
    const char *names[]={"PC=","RA=","SP="};
    const char hex[]="0123456789abcdef";
    char line[96];unsigned n=0;
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;names[i][j];++j)line[n++]=names[i][j];
        for(int j=15;j>=0;--j)line[n++]=hex[(words[i]>>(j*4))&15];
        line[n++]=' ';
    }
    line[n++]='\n';write(2,line,n);
    int fd=open("/proc/self/maps",O_RDONLY);
    if(fd>=0) {
        char buffer[1024];ssize_t count;
        while((count=read(fd,buffer,sizeof(buffer)))>0)write(2,buffer,(size_t)count);
        close(fd);
    }
    _exit(128+value);
}
static void lilos_install_trace(void)
{
    if(!getenv("LILSMOKE_NATIVE_TRACE"))return;
    struct sigaction action={0};action.sa_sigaction=lilos_trace_fault;action.sa_flags=SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV,&action,NULL);sigaction(SIGBUS,&action,NULL);sigaction(SIGILL,&action,NULL);
}
#endif
