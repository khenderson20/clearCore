# Golden test: memory — .data segment, la/lw/sw round trips, lbu/lhu
# sub-word loads (little-endian), and an immediate load-use pair that
# forces the pipelined core's stall path.
.data
vals:    .word 0x11223344, 0xcafebabe, -7, 0
scratch: .word 0, 0, 0, 0
.text
main:
    la   $s0, vals
    la   $s1, scratch
    lw   $t0, 0($s0)
    addu $t1, $t0, $t0         # load-use: needs a stall + forward
    lw   $t2, 4($s0)           # 0xcafebabe
    lbu  $t3, 4($s0)           # low byte  -> 0xbe
    lbu  $t4, 7($s0)           # high byte -> 0xca
    lhu  $t5, 4($s0)           # low half  -> 0xbabe
    lhu  $t6, 6($s0)           # high half -> 0xcafe
    lw   $t7, 8($s0)           # -7, sign-extended word
    sw   $t7, 0($s1)
    sw   $t3, 4($s1)
    lw   $s2, 0($s1)           # read back through memory
    lw   $s3, 4($s1)
    addu $s4, $s2, $s3
halt:
    j halt
