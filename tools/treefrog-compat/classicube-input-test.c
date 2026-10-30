/* SPDX-License-Identifier: MPL-2.0 */
#include "classicube-input.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    int16_t axes[2] = {0};
    const uint32_t buttons = (1u<<0)|(1u<<3)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<5)|
        (1u<<8)|(1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<13)|(1u<<14)|(1u<<15);
    assert(classicube_sticks(buttons, axes) == buttons);
    axes[0] = -32768;
    assert(classicube_sticks(1u<<7, axes) == ((1u<<7)|(1u<<15)));
    axes[0] = 32767;
    assert(classicube_sticks(1u<<5, axes) == ((1u<<5)|(1u<<13)));
    axes[0] = 0;
    axes[1] = -32768;
    assert(classicube_sticks(0, axes) == 1u<<12);
    axes[1] = 32767;
    assert(classicube_sticks(0, axes) == 1u<<14);
    axes[1] = 14000;
    assert(classicube_sticks(0, axes) == 0);
    puts("ClassiCube digital stick profile: PASS");
    return 0;
}
