#include <fcntl.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc<2)return 2;
    /* The network-disabled RA build omits socket_nonblock(stdin). */
    int flags=fcntl(STDIN_FILENO,F_GETFL);
    if(flags<0 || fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK)<0)return 1;
    execv(argv[1],argv+1);
    return 127;
}
