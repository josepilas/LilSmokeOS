#ifndef RKGAME_COMPAT_ZLIB_H
#define RKGAME_COMPAT_ZLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#define ZLIB_VERSION "1.2.11"
#define Z_OK 0
#define Z_NO_FLUSH 0
#define Z_NULL 0

typedef unsigned char Byte;
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef unsigned long uLongf;
typedef void *voidpf;
typedef void const *voidpc;

typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void (*free_func)(voidpf opaque, voidpf address);

struct internal_state;

typedef struct z_stream_s {
	Bytef *next_in;
	uInt avail_in;
	uLong total_in;
	Bytef *next_out;
	uInt avail_out;
	uLong total_out;
	char *msg;
	struct internal_state *state;
	alloc_func zalloc;
	free_func zfree;
	voidpf opaque;
	int data_type;
	uLong adler;
	uLong reserved;
} z_stream;

typedef z_stream *z_streamp;
typedef voidpf gzFile;

int inflate(z_streamp strm, int flush);
int inflateReset(z_streamp strm);
int inflateEnd(z_streamp strm);
int inflateInit2_(z_streamp strm, int windowBits, const char *version,
		  int stream_size);

#define inflateInit2(strm, windowBits) \
	inflateInit2_((strm), (windowBits), ZLIB_VERSION, (int)sizeof(z_stream))

int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source,
	       uLong sourceLen);
int compress(Bytef *dest, uLongf *destLen, const Bytef *source,
	     uLong sourceLen);

gzFile gzopen(const char *path, const char *mode);
gzFile gzdopen(int fd, const char *mode);
int gzread(gzFile file, void *buf, unsigned int len);
int gzwrite(gzFile file, voidpc buf, unsigned int len);
long gzseek(gzFile file, long offset, int whence);
int gzclose(gzFile file);

#ifdef __cplusplus
}
#endif

#endif
