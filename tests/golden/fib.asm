# Golden test: control flow — iterative Fibonacci(15) with a bne loop,
# plus taken and not-taken beq edges. Exercises branch resolution and,
# on the pipelined core, branch flushes.
.text
main:
    addi $t0, $zero, 15        # n
    addi $t1, $zero, 0         # fib(0)
    addi $t2, $zero, 1         # fib(1)
    addi $t3, $zero, 0         # i
loop:
    beq  $t3, $t0, done
    addu $t4, $t1, $t2         # next
    addu $t1, $zero, $t2
    addu $t2, $zero, $t4
    addi $t3, $t3, 1
    j    loop
done:
    beq  $t1, $zero, never     # not taken: fib(15) = 610 != 0
    addi $s0, $t1, 0           # copy result to $s0
    j    halt
never:
    addi $s1, $zero, 0x666     # must not execute
halt:
    j halt
