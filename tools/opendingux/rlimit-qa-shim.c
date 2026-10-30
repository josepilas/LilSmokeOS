/* QEMU-user applies a guest AS limit to its much larger host emulator.
 * Only for the isolated production-catalog QA command; never ship this. */
#include <sys/resource.h>
int setrlimit(int resource,const struct rlimit *limits)
{
	(void)resource;(void)limits;return 0;
}
