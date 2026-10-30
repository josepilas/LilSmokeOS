#include <stddef.h>
#include <stdio.h>
#include <linux/input.h>
#include <sys/time.h>

int main(void)
{
    printf("EVDEV_ABI long=%zu time_t=%zu timeval=%zu event=%zu type_offset=%zu\n",
           sizeof(long), sizeof(time_t), sizeof(struct timeval),
           sizeof(struct input_event), offsetof(struct input_event, type));
    return sizeof(long) == 4 && sizeof(struct input_event) != 16;
}
