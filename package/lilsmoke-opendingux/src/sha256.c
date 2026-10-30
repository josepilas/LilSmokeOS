#include "od.h"

void od_sha_init(struct od_sha *s)
{
	crypto_hash_sha256_init(&s->state);
}
void od_sha_update(struct od_sha *s,const void *data,size_t n)
{
	crypto_hash_sha256_update(&s->state,data,(unsigned long long)n);
}
void od_sha_final(struct od_sha *s,char hex[65])
{
	static const char digits[]="0123456789abcdef";
	unsigned char digest[crypto_hash_sha256_BYTES];
	unsigned i;
	crypto_hash_sha256_final(&s->state,digest);
	for(i=0;i<sizeof(digest);++i) {
		hex[i*2]=digits[digest[i]>>4];
		hex[i*2+1]=digits[digest[i]&15];
	}
	hex[64]=0;
}
int od_hash_fd(int fd,uint64_t size,char hex[65])
{
	struct od_sha s;unsigned char buf[32768];uint64_t at=0;
	od_sha_init(&s);
	while(at<size) {
		size_t n=sizeof(buf);if(n>size-at)n=(size_t)(size-at);
		if(od_fd_read(&fd,at,buf,n))return -1;
		od_sha_update(&s,buf,n);at+=n;
	}
	od_sha_final(&s,hex);return 0;
}
