#include <riscv.h>

    .text
    .globl switch_to
    .align(2)
//LAB4 function3 2510789
# 全局问题：为什么这一步必须手写汇编？
# C 编译器会在函数入口/出口自作主张地保存、恢复它认为需要的寄存器，
# 还会在函数返回时按它自己的约定回到调用者；而线程切换要求的恰恰相反：
# 只换 14 个寄存器、然后“返回”到另一个线程的返回地址。这种控制必须精确到单条指令，
# 所以最底层的现场搬运只能用汇编写。
#
# 全局问题：为什么只存这 14 个？
# RISC-V 调用约定把寄存器分成 caller-saved（t0~t6、a0~a7、ra）和 callee-saved（sp、s0~s11）。
# switch_to 是被 proc_run 调用的，调用点之前编译器已经把 caller-saved 里后面还要用的值
# 存进了 proc_run 的栈帧，所以这里只需负责 callee-saved 那一半，
# 再加上“回到哪”（ra）和“用哪个栈”（sp），正好 14 个。
# 多存不会错，只是多余；少存一个，该线程被换回时就会拿到别人的寄存器值，表现为“偶发跑飞”。
# ============================================================
# void switch_to(struct context *from, struct context *to)
#   a0 = from：当前（被换下）线程的 context 地址
#   a1 = to  ：下一个（要被换入）线程的 context 地址
#
# struct context 的字段顺序（见 kern/process/proc.h）：
#   ra, sp, s0, s1, s2, ..., s11   —— 共 14 个 uintptr_t，偏移 0 ~ 13*8
#
# 本函数只负责“把现场存进 from，把 to 的现场装回 CPU”，
# 不做任何“该选谁”的判断——那是 schedule() 的事。
# 所以它是调度器最底层的一块，和调度策略完全解耦。
#
# 和 trapentry.S 里 RESTORE_ALL 的一个重要区别：
#   这里所有存取都以 a0/a1（普通指针）为基址，sp 不是基址，
#   所以“sp 必须最后恢复”那条约束在这里不成立，顺序可以自由。
# ============================================================
switch_to:
    # ---------- 第一步：保存 from 的现场 ----------
    STORE ra,  0*REGBYTES(a0)     # 返回地址：switch_to 执行完该回到哪里
    STORE sp,  1*REGBYTES(a0)     # 栈指针：每个线程都有自己的内核栈
    STORE s0,  2*REGBYTES(a0)     # s0~s11 是“被调用者保存”寄存器，
    STORE s1,  3*REGBYTES(a0)     # C 编译器认为跨函数调用它们不变，
    STORE s2,  4*REGBYTES(a0)     # 所以这里必须由我们自己保存/恢复
    STORE s3,  5*REGBYTES(a0)
    STORE s4,  6*REGBYTES(a0)
    STORE s5,  7*REGBYTES(a0)
    STORE s6,  8*REGBYTES(a0)
    STORE s7,  9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)

    # ---------- 第二步：恢复 to 的现场 ----------
    # 对“刚创建、还没运行过”的线程（如 initproc），
    # to->context.ra 在 copy_thread 里被设成 forkret，
    # to->context.sp 被设成该线程 trapframe 的地址；
    # 所以下面的 ret 一跳，就落到 forkret，开始它的第一次运行。
    LOAD ra,  0*REGBYTES(a1)
    LOAD sp,  1*REGBYTES(a1)
    LOAD s0,  2*REGBYTES(a1)
    LOAD s1,  3*REGBYTES(a1)
    LOAD s2,  4*REGBYTES(a1)
    LOAD s3,  5*REGBYTES(a1)
    LOAD s4,  6*REGBYTES(a1)
    LOAD s5,  7*REGBYTES(a1)
    LOAD s6,  8*REGBYTES(a1)
    LOAD s7,  9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)

    # ---------- 第三步：真正的“切换” ----------
    # ret = jr ra：跳到刚恢复出来的返回地址。
    #   - 若是“运行过的线程”：回到它上次 switch_to 之后的位置（proc_run 里）
    #   - 若是“新线程”：ra = forkret，于是从 forkret 开始第一次运行
    ret
