# Golden test: shifts — sll/srl/sra (immediate) and sllv/srlv (variable),
# with a negative value to expose arithmetic-vs-logical right shift.
.text
main:
    lui  $t0, 0x8000
    ori  $t0, $t0, 0x00f0      # 0x800000f0 (negative)
    sll  $t1, $t0, 4
    srl  $t2, $t0, 4           # logical: zero-fills
    sra  $t3, $t0, 4           # arithmetic: sign-fills
    addi $t4, $zero, 8
    sllv $t5, $t0, $t4
    srlv $t6, $t0, $t4
    sra  $t7, $t0, 31          # all sign bits
    srl  $s0, $t0, 31          # just the sign bit
    sll  $s1, $t0, 0           # identity
halt:
    j halt
