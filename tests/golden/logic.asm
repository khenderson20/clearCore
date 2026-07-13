# Golden test: bitwise logic — andi/ori/xori (zero-extended), lui, and/or/xor/nor.
.text
main:
    lui  $t0, 0xdead
    ori  $t0, $t0, 0xbeef      # 0xdeadbeef
    lui  $t1, 0x1234
    ori  $t1, $t1, 0x5678      # 0x12345678
    and  $t2, $t0, $t1
    or   $t3, $t0, $t1
    xor  $t4, $t0, $t1
    nor  $t5, $t0, $t1
    andi $t6, $t0, 0xffff      # zero-extends: 0x0000beef
    xori $t7, $t0, 0xffff
    ori  $s0, $zero, 0
    nor  $s1, $zero, $zero     # all ones
halt:
    j halt
