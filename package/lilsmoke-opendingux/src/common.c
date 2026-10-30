#include "od.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* QEMU-user remaps "/" to its loader sysroot. Only the hosted QA build
 * uses proc's real process root; production always anchors on "/". */
#ifdef OD_HOSTED_QA
#define OD_ABSOLUTE_ROOT "/proc/self/root"
#else
#define OD_ABSOLUTE_ROOT "/"
#endif

static struct timespec end_time;
void od_deadline(unsigned seconds)
{
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	end_time.tv_sec += seconds;
}
int od_expired(void)
{
	struct timespec now;
	if (!end_time.tv_sec) return 0;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec > end_time.tv_sec ||
		(now.tv_sec == end_time.tv_sec && now.tv_nsec >= end_time.tv_nsec);
}
int od_fail(char *reason, const char *text)
{
	if (!reason[0]) od_clean(reason, OD_REASON, text, strlen(text));
	return -1;
}
void od_clean(char *out, size_t cap, const char *in, size_t n)
{
	size_t i;
	if (!cap) return;
	if (n >= cap) n = cap - 1;
	for (i = 0; i < n; ++i) {
		unsigned char c = (unsigned char)in[i];
		out[i] = c < 32 || c == 127 ? ' ' : (char)c;
	}
	out[n] = 0;
}
int od_join(char *out, const char *a, const char *b)
{
	int n = snprintf(out, OD_PATH, "%s%s%s", a, *a ? "/" : "", b);
	return n < 0 || n >= OD_PATH ? -1 : 0;
}
int od_component(const char *s)
{
	size_t n = strlen(s);
	if (!n || n > 120 || !strcmp(s, ".") || !strcmp(s, "..") ||
	    s[n - 1] == '.' || s[n - 1] == ' ') return 0;
	for (; *s; ++s)
		if ((unsigned char)*s < 32 || (unsigned char)*s >= 127 ||
		    strchr("/\\:*?\"<>|", *s)) return 0;
	return 1;
}
int od_relative(const char *s)
{
	char part[121];
	size_t n;
	if (!*s || strlen(s) >= OD_PATH) return 0;
	while (*s) {
		n = strcspn(s, "/");
		if (!n || n >= sizeof(part)) return 0;
		memcpy(part, s, n); part[n] = 0;
		if (!od_component(part)) return 0;
		s += n;
		if (*s && !*++s) return 0;
	}
	return 1;
}
int od_fd_read(void *ctx, uint64_t at, void *data, size_t n)
{
	int fd = *(int *)ctx;
	unsigned char *p = data;
	if (at > INT64_MAX || n > INT64_MAX - at || od_expired()) return -1;
	while (n) {
		ssize_t got = pread(fd, p, n, (off_t)at);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) return -1;
		p += got; n -= (size_t)got; at += (size_t)got;
	}
	return 0;
}
int od_write(int fd, const void *data, size_t n)
{
	const unsigned char *p = data;
	while (n) {
		ssize_t k = write(fd, p, n);
		if (k < 0 && errno == EINTR) continue;
		if (k <= 0 || od_expired()) return -1;
		p += k; n -= (size_t)k;
	}
	return 0;
}
/* Walk every component with openat: O_NOFOLLOW on just the leaf is not enough. */
int od_open_path(const char *path, int flags)
{
	char buf[OD_PATH], *s, *next;
	int dir, fd;
	if (strlen(path) >= sizeof(buf)) return -1;
	strcpy(buf, path);
	dir = open(path[0] == '/' ? OD_ABSOLUTE_ROOT : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dir < 0) return -1;
	s = buf + (path[0] == '/');
	while (*s) {
		next = strchr(s, '/');
		if (next) *next = 0;
		if (!od_component(s)) { close(dir); return -1; }
		fd = openat(dir, s, (next ? O_RDONLY | O_DIRECTORY : flags) |
			    O_NOFOLLOW | O_CLOEXEC);
		close(dir);
		if (fd < 0) return -1;
		dir = fd;
		if (!next) return dir;
		s = next + 1;
	}
	close(dir); return -1;
}
int od_mkdir_path(const char *path)
{
	char buf[OD_PATH], *s, *next;
	int dir, fd;
	if (strlen(path) >= sizeof(buf)) return -1;
	strcpy(buf, path);
	dir = open(path[0] == '/' ? OD_ABSOLUTE_ROOT : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dir < 0) return -1;
	s = buf + (path[0] == '/');
	while (*s) {
		next = strchr(s, '/');
		if (next) *next = 0;
		if (!od_component(s) || (mkdirat(dir, s, 0700) && errno != EEXIST)) {
			close(dir); return -1;
		}
		fd = openat(dir, s, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		close(dir);
		if (fd < 0) return -1;
		dir = fd;
		if (!next) return dir;
		s = next + 1;
	}
	close(dir); return -1;
}
int od_remove_at(int parent, const char *name, unsigned depth)
{
	struct stat st;
	struct dirent *ent;
	DIR *dir;
	int fd, result = 0;
	unsigned count = 0;
	if (!od_component(name) || depth > OD_DEPTH ||
	    fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) return -1;
	if (!S_ISDIR(st.st_mode)) return unlinkat(parent, name, 0);
	fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0 || !(dir = fdopendir(fd))) { if (fd >= 0) close(fd); return -1; }
	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
		if (++count > OD_NODES || od_expired() ||
		    od_remove_at(fd, ent->d_name, depth + 1)) { result = -1; break; }
	}
	closedir(dir);
	return result ? result : unlinkat(parent, name, AT_REMOVEDIR);
}
