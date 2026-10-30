/***************************************************************************
    Binary File Loader. 
    
    Handles loading an individual binary file to memory.
    Supports reading bytes, words and longs from this area of memory.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <cstddef>       // for std::size_t
#include <string>
#include <boost/crc.hpp> // CRC Checking via Boost library.

#include <stdint.h>
#include "romloader.hpp"

#include <libretro.h>
#include <streams/file_stream.h>

extern retro_log_printf_t                 log_cb;

extern "C" {
RFILE* rfopen(const char *path, const char *mode);
int rfclose(RFILE* stream);
int64_t rfread(void* buffer,
   size_t elem_size, size_t elem_count, RFILE* stream);
}

RomLoader::RomLoader()
{
    loaded = false;
}

RomLoader::~RomLoader()
{
}

void RomLoader::init(const uint32_t length)
{
    this->length = length;
    rom = new uint8_t[length];
}

void RomLoader::unload(void)
{
    delete[] rom;
}

int RomLoader::load(const char* filename, const int offset, const int length, const uint32_t expected_crc, const uint8_t interleave)
{
    extern retro_environment_t environ_cb;
    extern char rom_path[1024];
    const char *dir   = NULL;
    std::string path  = std::string(rom_path) + std::string(filename);
    // Open rom file
    RFILE *src        = rfopen(path.c_str(), "rb");
    if (!src)
    {
        log_cb(RETRO_LOG_ERROR, "Cannot open ROM: %s\n", filename);
        loaded = false;
        return 1; // fail
    }

    // Read file
    char* buffer  = new char[length];
    size_t gcount = rfread(buffer, sizeof(char), length, src);

    // Check CRC on file
    boost::crc_32_type result;
    result.process_bytes(buffer, gcount);

    if (expected_crc != result.checksum())
    {
        log_cb(RETRO_LOG_ERROR, "%s has incorrect checksum. Expected: 0x%08x, Found: 0x%08x\n", filename, expected_crc, result.checksum());
    }

    // Interleave file as necessary
    for (int i = 0; i < length; i++)
        rom[(i * interleave) + offset] = buffer[i];

    // Clean Up
    delete[] buffer;
    rfclose(src);
    loaded = true;
    return 0; // success
}

// Load Binary File (LayOut Levels, Tilemap Data etc.)
int RomLoader::load_binary(const char* filename)
{
    // --------------------------------------------------------------------------------------------
    // Read LayOut Data File
    // --------------------------------------------------------------------------------------------
    RFILE *src = rfopen(filename, "rb");
    if (!src)
    {
        log_cb(RETRO_LOG_ERROR, "Cannot open file: %s\n", filename);
        loaded = false;
        return 1; // fail
    }

    length       = filestream_get_size(src);

    // Read file
    char* buffer = new char[length];
    rfread(buffer, sizeof(char), length, src);
    rom = (uint8_t*) buffer;

    // Clean Up
    rfclose(src);

    loaded = true;
    return 0; // success
}
