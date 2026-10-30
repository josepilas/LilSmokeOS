/* QA-only presenter: validates real SDL engine frames without vendor ioctls. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static unsigned frames;
int lilsmoke_present_rgb(const void *pixels,unsigned w,unsigned h,unsigned pitch,
                        int rgb32,unsigned num,unsigned den)
{
    assert(pixels&&w&&h&&w<=640&&h<=480&&pitch>=w*2&&!rgb32&&num&&den);
    if(!frames++)fprintf(stderr,"QA_NATIVE_PRESENT source=%ux%u pitch=%u aspect=%u:%u\n",w,h,pitch,num,den);
    return 1;
}
void lilsmoke_present_reset(void){}
