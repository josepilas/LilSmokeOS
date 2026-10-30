/* SPDX-License-Identifier: MPL-2.0 */
/* Original locally extracted bytes, in the original MIPS object's layout. */
#if !defined(__mips__) || !defined(__MIPSEL__)
#error This asset object requires the little-endian MIPS target
#endif
    .abicalls
    .set noreorder
    .data

    .balign 16
    .globl gBankSetsData
    .type gBankSetsData, @object
gBankSetsData:
    .incbin "build/original-mips/sound/bank_sets"
    .size gBankSetsData, .-gBankSetsData

    .balign 16
    .globl gMusicData
    .type gMusicData, @object
gMusicData:
    .incbin "build/original-mips/sound/sequences.bin"
    .size gMusicData, .-gMusicData

    .balign 16
    .globl gSoundDataRaw
    .type gSoundDataRaw, @object
gSoundDataRaw:
    .incbin "build/original-mips/sound/sound_data.tbl"
    .size gSoundDataRaw, .-gSoundDataRaw

    .balign 16
    .globl gSoundDataADSR
    .type gSoundDataADSR, @object
gSoundDataADSR:
    .incbin "build/original-mips/sound/sound_data.ctl"
    .size gSoundDataADSR, .-gSoundDataADSR

    .section .note.GNU-stack,"",@progbits
