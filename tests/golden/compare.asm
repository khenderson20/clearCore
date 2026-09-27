# Golden test: comparisons — slt/sltu/slti/sltiu at the signed/unsigned
# boundaries (INT_MIN, -1 vs UINT_MAX, sign-extended immediates).
.text
main:
    lui   $t0, 0x8000          # INT_MIN / large unsigned
    addi  $t1, $zero, -1       # -1 / UINT_MAX
    addi  $t2, $zero, 1
    slt   $t3, $t0, $t2        # signed:   INT_MIN < 1  -> 1
    sltu  $t4, $t0, $t2        # unsigned: huge < 1     -> 0
    slt   $t5, $t1, $zero      # -1 < 0                 -> 1
    sltu  $t6, $t1, $zero      # UINT_MAX < 0           -> 0
    slti  $t7, $t0, -1         # INT_MIN < -1           -> 1
    sltiu $s0, $t2, -1         # imm sign-extends to UINT_MAX -> 1
    slt   $s1, $t2, $t2        # equal                  -> 0
    sltu  $s2, $zero, $t1      # 0 < UINT_MAX           -> 1
halt:
    j halt
