# SPDX-License-Identifier: MIT
# LilSmokeOS's own emulator fixture. No Nintendo IPL or commercial game code.
# Runs from IPL3 DMEM under the core's HLE bootstrap, drives RDP fill, VI,
# and SI controller DMA. Start changes red to green; N64 A changes it to blue.
# This is a software regression fixture, not a cartridge bootloader.
.set noreorder
.set noat
.text
.globl _start
_start:
    mtc0 $zero, $12
    nop
    lui $s0, 0xa440
    li $t0, 0x3202
    sw $t0, 0($s0)
    lui $t0, 0x0010
    sw $t0, 4($s0)
    li $t0, 320
    sw $t0, 8($s0)
    li $t0, 2
    sw $t0, 12($s0)
    li $t0, 0x03e52239
    sw $t0, 20($s0)
    li $t0, 525
    sw $t0, 24($s0)
    li $t0, 0x0c15
    sw $t0, 28($s0)
    li $t0, 0x0c150c15
    sw $t0, 32($s0)
    li $t0, 0x006c02ec
    sw $t0, 36($s0)
    li $t0, 0x002501ff
    sw $t0, 40($s0)
    li $t0, 0x000e0204
    sw $t0, 44($s0)
    li $t0, 0x0200
    sw $t0, 48($s0)
    li $t0, 0x0400
    sw $t0, 52($s0)
    lui $s1, 0xa480
    lui $s2, 0xa000
    addiu $s2, $s2, 0x0200
    move $t0, $s2
    li $t1, 64
clear_pif:
    sb $zero, 0($t0)
    addiu $t1, $t1, -1
    bnez $t1, clear_pif
    addiu $t0, $t0, 1
    lui $s3, 0xa000
    addiu $s3, $s3, 0x1000
    li $t0, 0xff10013f
    sw $t0, 0($s3)
    lui $t0, 0x0010
    sw $t0, 4($s3)
    li $t0, 0xed000000
    sw $t0, 8($s3)
    li $t0, 0x005003c0
    sw $t0, 12($s3)
    li $t0, 0xefb00000
    sw $t0, 16($s3)
    sw $zero, 20($s3)
    li $t0, 0xf7000000
    sw $t0, 24($s3)
    li $t0, 0xf64fc3bc
    sw $t0, 32($s3)
    sw $zero, 36($s3)
    li $t0, 0xe9000000
    sw $t0, 40($s3)
    sw $zero, 44($s3)
    lui $s4, 0xa410
loop:
    li $t0, 0x01040100
    sw $t0, 0($s2)
    li $t0, 0x000000fe
    sw $t0, 4($s2)
    li $t0, 1
    sb $t0, 63($s2)
    li $t0, 0x0200
    sw $t0, 0($s1)
    li $t0, 0x1fc007c0
    sw $t0, 16($s1)
wait_write:
    lw $t1, 24($s1)
    andi $t1, $t1, 3
    bnez $t1, wait_write
    nop
    sw $zero, 24($s1)
    sw $t0, 4($s1)
wait_read:
    lw $t1, 24($s1)
    andi $t1, $t1, 3
    bnez $t1, wait_read
    nop
    sw $zero, 24($s1)
    lbu $t1, 3($s2)
    li $t2, 0xf801f801
    andi $t3, $t1, 0x10
    beqz $t3, test_a
    nop
    li $t2, 0x07c107c1
test_a:
    andi $t3, $t1, 0x80
    beqz $t3, draw
    nop
    li $t2, 0x003f003f
draw:
    sw $t2, 28($s3)
    li $t0, 0x1000
    sw $t0, 0($s4)
    li $t0, 0x1030
    sw $t0, 4($s4)
    # Wait for scanout to move across a field before the next controller poll.
wait_bottom:
    lw $t0, 16($s0)
    sltiu $t1, $t0, 400
    bnez $t1, wait_bottom
    nop
wait_top:
    lw $t0, 16($s0)
    sltiu $t1, $t0, 40
    beqz $t1, wait_top
    nop
    b loop
    nop
