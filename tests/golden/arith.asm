# Golden test: basic arithmetic — addi/addiu/add/addu/sub/subu, negative values.
.text
main:
    addi  $t0, $zero, 100
    addi  $t1, $zero, -37
    add   $t2, $t0, $t1        # 63
    sub   $t3, $t1, $t0        # -137
    addiu $t4, $zero, -1       # 0xffffffff
    addu  $t5, $t4, $t4        # 0xfffffffe (wraps, no trap)
    subu  $t6, $zero, $t0      # -100
    addi  $s0, $t2, 0x7ff      # 63 + 2047
    add   $s1, $s0, $t3        # mixed signs
    addiu $s2, $t6, 100        # back to zero
halt:
    j halt
