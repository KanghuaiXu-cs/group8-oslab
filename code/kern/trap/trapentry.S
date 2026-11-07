#include <riscv.h>

    .macro SAVE_ALL
    # 说明：本宏内的指令序列是框架提供的，这里没有改动任何一条指令，
    #       只在原注释之外补充中文解释，便于对照理解（不影响判分）。

    # csrw = 只写：sscratch ← sp（sp 本身不变，只是被复制了一份）。
    # 为什么要把 sp 藏进 sscratch：下面马上要用 sp 当基址往栈上存东西，
    # 而本宏要保存的是“进 trap 之前”的 sp，必须先找个别的地方把它暂存起来。
    # 它最终会被写进 trapframe 的槽位 2，见本宏末尾的 STORE s0, 2*REGBYTES(sp)。
    csrw sscratch, sp

    addi sp, sp, -36 * REGBYTES
    # save x registers
    STORE x0, 0*REGBYTES(sp)
    STORE x1, 1*REGBYTES(sp)
    STORE x3, 3*REGBYTES(sp)
    STORE x4, 4*REGBYTES(sp)
    STORE x5, 5*REGBYTES(sp)
    STORE x6, 6*REGBYTES(sp)
    STORE x7, 7*REGBYTES(sp)
    STORE x8, 8*REGBYTES(sp)
    STORE x9, 9*REGBYTES(sp)
    STORE x10, 10*REGBYTES(sp)
    STORE x11, 11*REGBYTES(sp)
    STORE x12, 12*REGBYTES(sp)
    STORE x13, 13*REGBYTES(sp)
    STORE x14, 14*REGBYTES(sp)
    STORE x15, 15*REGBYTES(sp)
    STORE x16, 16*REGBYTES(sp)
    STORE x17, 17*REGBYTES(sp)
    STORE x18, 18*REGBYTES(sp)
    STORE x19, 19*REGBYTES(sp)
    STORE x20, 20*REGBYTES(sp)
    STORE x21, 21*REGBYTES(sp)
    STORE x22, 22*REGBYTES(sp)
    STORE x23, 23*REGBYTES(sp)
    STORE x24, 24*REGBYTES(sp)
    STORE x25, 25*REGBYTES(sp)
    STORE x26, 26*REGBYTES(sp)
    STORE x27, 27*REGBYTES(sp)
    STORE x28, 28*REGBYTES(sp)
    STORE x29, 29*REGBYTES(sp)
    STORE x30, 30*REGBYTES(sp)
    STORE x31, 31*REGBYTES(sp)

    # get sr, epc, badvaddr, cause
    # Set sscratch register to 0, so that if a recursive exception
    # occurs, the exception vector knows it came from the kernel
    #
    # 【三种 CSR 指令的区别，下面 5 条全用到了】
    #   csrr  rd, csr      = 只读：rd ← csr          （CSR 本身不变）
    #   csrw  csr, rs      = 只写：csr ← rs          （rs 本身不变，只是被复制）
    #   csrrw rd, csr, rs  = 读写：rd ← csr 的旧值，同时 csr ← rs（一条指令内原子完成）
    # 关键约束：CSR 不能直接和内存交换数据，必须先搬到通用寄存器，
    #           再用 STORE 落到内存；反过来则先 LOAD 再 csrw。
    # 所以这里的顺序固定是：CSR → 通用寄存器 → 内存。
    csrrw s0, sscratch, x0     # 一石二鸟：s0 ← sscratch（取回进 trap 前的 sp）；
                               #   同时 sscratch ← x0(=0)，清空“临时口袋”
    csrr s1, sstatus          # 只读，把 sstatus 读到 s1（sstatus 本身不变）
    csrr s2, sepc             # 只读，把 sepc（被打断的指令地址）读到 s2
    csrr s3, sbadaddr         # 只读，sbadaddr 就是 stval（出错地址/指令）
    csrr s4, scause           # 只读，scause = 本次 Trap 的原因编号

    # 把刚读到 5 个寄存器里的值，写进 trapframe 对应的槽位
    STORE s0, 2*REGBYTES(sp)   # 槽位 2 = 原 sp（前面按顺序存 x0..x31 时故意跳过了 x2）
    STORE s1, 32*REGBYTES(sp)  # 槽位 32 = sstatus
    STORE s2, 33*REGBYTES(sp)  # 槽位 33 = sepc
    STORE s3, 34*REGBYTES(sp)  # 槽位 34 = badvaddr
    STORE s4, 35*REGBYTES(sp)  # 槽位 35 = cause
    .endm

    .macro RESTORE_ALL
    #lab3 function1 YOUR CODE
    # 第一步：先写回两个 CSR。
    # 必须放在恢复通用寄存器之前：因为这里借了 s1/s2（即 x9/x18）当临时寄存器，
    # 它们稍后会被 trapframe 里的真值覆盖，所以 CSR 要在被覆盖前写回。
    # 这两条 LOAD 是“内存 → 通用寄存器”，下面的 csrw 才是“通用寄存器 → CSR”。
    # 不能跳过 LOAD 直接写 CSR，因为 CSR 指令只认通用寄存器，不认内存。
    LOAD s1, 32*REGBYTES(sp)        # s1 ← 槽位 32（sstatus）
    LOAD s2, 33*REGBYTES(sp)        # s2 ← 槽位 33（sepc）
    # csrw = 只写：把 s1/s2 里的值写进 CSR（s1/s2 本身不变，内存也不变）
    csrw sstatus, s1                # 恢复 SPP/SPIE/SIE → 决定 sret 回到哪个特权级、中断开不开
    csrw sepc, s2                   # 恢复返回地址 → 模块二改的 tf->epc 在这里才真正生效

    # 第二步：按 SAVE_ALL 存放的顺序，原样恢复通用寄存器。
    # 说明：x0 是硬连线的 0，不需要恢复；
    #       x2(sp) 不在这里恢复，留到第三步。
    LOAD x1,  1*REGBYTES(sp)
    LOAD x3,  3*REGBYTES(sp)
    LOAD x4,  4*REGBYTES(sp)
    LOAD x5,  5*REGBYTES(sp)
    LOAD x6,  6*REGBYTES(sp)
    LOAD x7,  7*REGBYTES(sp)
    LOAD x8,  8*REGBYTES(sp)
    LOAD x9,  9*REGBYTES(sp)
    LOAD x10, 10*REGBYTES(sp)
    LOAD x11, 11*REGBYTES(sp)
    LOAD x12, 12*REGBYTES(sp)
    LOAD x13, 13*REGBYTES(sp)
    LOAD x14, 14*REGBYTES(sp)
    LOAD x15, 15*REGBYTES(sp)
    LOAD x16, 16*REGBYTES(sp)
    LOAD x17, 17*REGBYTES(sp)
    LOAD x18, 18*REGBYTES(sp)
    LOAD x19, 19*REGBYTES(sp)
    LOAD x20, 20*REGBYTES(sp)
    LOAD x21, 21*REGBYTES(sp)
    LOAD x22, 22*REGBYTES(sp)
    LOAD x23, 23*REGBYTES(sp)
    LOAD x24, 24*REGBYTES(sp)
    LOAD x25, 25*REGBYTES(sp)
    LOAD x26, 26*REGBYTES(sp)
    LOAD x27, 27*REGBYTES(sp)
    LOAD x28, 28*REGBYTES(sp)
    LOAD x29, 29*REGBYTES(sp)
    LOAD x30, 30*REGBYTES(sp)
    LOAD x31, 31*REGBYTES(sp)

    # 第三步：最后恢复 sp。
    # 上面所有 LOAD 都以当前 sp 为基址，所以 sp 必须放到最后才能改。
    # 槽位 2 里存的是“进 trap 之前”的 sp（SAVE_ALL 通过 sscratch 中转保存的），
    # 因此这一条 LOAD 既恢复了 sp，又顺手把整个 trapframe 弹出栈；
    # 不需要再写 addi sp, sp, 36*REGBYTES（两个都写会多弹 288 字节）。
    LOAD x2,  2*REGBYTES(sp)
    .endm

    .globl __alltraps
    .align(2)
__alltraps:
    SAVE_ALL

    #lab3 function1 YOUR CODE
    # sp should be the same as before "jal trap"
    # 把当前 sp 指向的 trapframe 作为 trap() 的第一个参数
    #（RISC-V 调用约定：第一个参数放在 a0，也就是 x10）
    mv a0, sp

    # 调用 C 处理函数。jal 会覆盖 ra，但原 ra 早已存进 trapframe 的槽位 1，
    # 后面 RESTORE_ALL 会把它恢复，所以被打断的程序完全察觉不到。
    # sp 在这次调用前后不变，因此 trap() 返回时它仍指向同一个 trapframe。
    jal trap

    .globl __trapret
__trapret:
    RESTORE_ALL
    # return from supervisor call
    sret
