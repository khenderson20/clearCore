# Golden test: calls — jal/jr with a leaf function, plus jalr through a
# register. $ra values are comparable because both MARS (CompactTextAtZero)
# and clearCore place .text at address 0.
.text
main:
    addi $a0, $zero, 21
    jal  double                # $v0 = 42
    addu $s0, $zero, $v0
    la   $t9, triple
    jalr $t9                   # $v0 = 126 (uses previous $v0 via $a0 below)
    addu $s1, $zero, $v0
    j    halt
double:
    addu $v0, $a0, $a0
    jr   $ra
triple:
    addu $v0, $s0, $s0
    addu $v0, $v0, $s0
    jr   $ra
halt:
    j halt
