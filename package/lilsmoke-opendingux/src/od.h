#ifndef LILSMOKE_OD_H
#define LILSMOKE_OD_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sodium/crypto_hash_sha256.h>

#define OD_PATH 1024
#define OD_META 16384
#define OD_ARGS 32
#define OD_ARG 512
#define OD_REASON 192
#define OD_CATALOG 128
#define OD_NODES 4096
#define OD_DEPTH 16
#define OD_IMAGE_MAX (256ULL * 1024 * 1024)
#define OD_CACHE_MAX (512ULL * 1024 * 1024)
#define OD_FILE_MAX (128ULL * 1024 * 1024)
struct od_reader {
	void *ctx;
	uint64_t size;
	int (*read)(void *, uint64_t, void *, size_t);
};
struct od_desktop {
	char name[128], systems[256], exec[2048], menu_exec[2048], reason[OD_REASON];
	char args[OD_ARGS][OD_ARG];
	unsigned argc;
	int needs_rom;
};
struct od_sha {
	crypto_hash_sha256_state state;
};
void od_sha_init(struct od_sha *);
void od_sha_update(struct od_sha *, const void *, size_t);
void od_sha_final(struct od_sha *, char hex[65]);
int od_hash_fd(int fd, uint64_t size, char hex[65]);
int od_fail(char *, const char *);
int od_join(char *, const char *, const char *);
int od_component(const char *);
int od_relative(const char *);
void od_clean(char *, size_t, const char *, size_t);
int od_parse(const char *, size_t, struct od_desktop *);
int od_lex(struct od_desktop *, const char *);
int od_elf(const struct od_reader *, char *, int);
int od_fd_read(void *, uint64_t, void *, size_t);
int od_open_path(const char *, int);
int od_mkdir_path(const char *);
int od_remove_at(int, const char *, unsigned);
int od_write(int, const void *, size_t);
void od_deadline(unsigned);
int od_expired(void);

struct od_opk;
typedef int (*od_entry_cb)(struct od_opk *, const char *, const struct od_reader *, unsigned, void *);
struct od_opk *od_opk_open(int, char *);
void od_opk_close(struct od_opk *);
int od_opk_walk(struct od_opk *, od_entry_cb, void *, char *);
int od_opk_reader(struct od_opk *, const char *, struct od_reader *, void **, char *);
void od_opk_reader_free(void *);
int od_opk_extract(struct od_opk *, int, int, uint64_t *, char *);
#endif
