/* SPDX-License-Identifier: MPL-2.0
 * miniz has no public writer permission setter. This small extension is
 * compiled with the pinned implementation, without modifying upstream files.
 */
#include "miniz_zip.c"
int tf_zip_mode(mz_zip_archive *zip,unsigned mode) {
    mz_zip_internal_state *state=zip->m_pState;
    if(!state||!zip->m_total_files||zip->m_zip_mode!=MZ_ZIP_MODE_WRITING)return 0;
    size_t index=zip->m_total_files-1;
    if(index>=state->m_central_dir_offsets.m_size)return 0;
    mz_uint32 offset=((mz_uint32*)state->m_central_dir_offsets.m_p)[index];
    if(offset>state->m_central_dir.m_size||MZ_ZIP_CENTRAL_DIR_HEADER_SIZE>state->m_central_dir.m_size-offset)return 0;
    mz_uint8 *header=(mz_uint8*)state->m_central_dir.m_p+offset;
    MZ_WRITE_LE16(header+MZ_ZIP_CDH_VERSION_MADE_BY_OFS,(3<<8)|20);
    MZ_WRITE_LE32(header+38,(0100000u|(mode&0777u))<<16);
    return 1;
}
