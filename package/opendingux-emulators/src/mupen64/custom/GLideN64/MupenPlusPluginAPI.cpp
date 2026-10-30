#include "PluginAPI.h"
#include "Types.h"
#include "OpenGL.h"

extern "C" {

EXPORT int CALL RomOpen(void)
{
	api().RomOpen();
	return 1;
}

EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
}

EXPORT void CALL SetRenderingCallback(void (*callback)(int))
{
}

} // extern "C"
