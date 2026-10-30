#ifndef OD_INPUT_WIRE_H
#define OD_INPUT_WIRE_H
#include <linux/input.h>
#include <stdint.h>
#if (defined(__mips__) && __SIZEOF_POINTER__ == 4) || defined(OD_INPUT_WIRE_QA)
struct od_kernel_input_event {
	int32_t seconds, microseconds;
	uint16_t type, code;
	int32_t value;
};
#ifdef __cplusplus
static_assert(sizeof(od_kernel_input_event)==16,"Stock kernel 4.4 evdev wire ABI");
#else
_Static_assert(sizeof(struct od_kernel_input_event)==16,"Stock kernel 4.4 evdev wire ABI");
#endif
#define input_event od_kernel_input_event
#endif
#endif
