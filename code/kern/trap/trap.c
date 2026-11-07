#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <sbi.h>
#include <stdio.h>
#include <trap.h>

/* ============================================================================
 *  Lab3 陷阱处理总览：硬件"打断" -> 内核"接管" -> 原样"返回"
 * ============================================================================
 *  答辩时可以照这条链从头讲到尾，它是整个实验的骨架：
 *
 *  1) 出事的那一刻，硬件干两件事：
 *       把"打断在哪条指令"记到 sepc，把"为什么打断"记到 scause，
 *       然后强行把 PC 跳到 stvec 指向的地址 —— 也就是 trapentry.S 的 __alltraps。
 *       当前指令流就这么被"劫持"了。
 *
 *  2) __alltraps 第一件事是 SAVE_ALL：
 *       在栈上开一块 36 * 8 字节的空间，把 32 个通用寄存器 + 4 个 CSR
 *       一个槽位一个槽位摊进去。这块内存就是 struct trapframe。
 *       为什么要存？因为接下来要跑 C 代码，C 代码会把寄存器用个遍，
 *       不先把现场抄下来，回来时就没法还原了。
 *
 *  3) 把这坨内存的首地址塞进 a0（RISC-V 调用约定的第一个参数），
 *     然后 jal trap —— 进入 C 世界。
 *
 *  4) trap() -> trap_dispatch() 看 scause 最高位分流：
 *       最高位 = 1  -> interrupt_handler()   （中断：外面的事）
 *       最高位 = 0  -> exception_handler()   （异常：这条指令自己的问题）
 *
 *  5) 处理完一路返回，落到 __trapret 的 RESTORE_ALL：
 *     把 trapframe 里那 36 个值按原槽位写回寄存器和 CSR。
 *
 *  6) sret 从 sepc 继续执行。被打断的程序完全感知不到中间发生过什么。
 *
 *  一句话：整个过程就是"抄现场 -> 处理 -> 按抄下来的原样恢复"。
 *  所以 SAVE_ALL 和 RESTORE_ALL 必须像照镜子一样严格对称，
 *  槽位错一个，程序回来就跑飞。
 * ============================================================================ */

#define TICK_NUM 100
#define SHUTDOWN_AFTER 10

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - 告诉硬件"出了事该跳哪去"，是整条 trap 链的起点
 *
 * 只干两件事，但两件都是必须的：
 *
 *   1) stvec = &__alltraps
 *      stvec 是"陷阱入口地址"。从这一刻起，任何中断或异常发生，
 *      硬件都会把 PC 强行跳到 __alltraps —— 也就是 trapentry.S 里那段
 *      保存现场、调用 C 函数的汇编。没有这一步，trap 根本进不来。
 *
 *   2) sscratch = 0
 *      这个寄存器在 RISC-V 里没有规定用途，由内核自己约定。
 *      本实验把它当"临时寄存处"用：SAVE_ALL 一进门就
 *        csrw sscratch, sp       把原始 sp 暂时存进去
 *        csrrw s0, sscratch, x0  再取出来、同时把 sscratch 清零
 *      为什么要绕这一道？因为 sp 自己就是"指向 trapframe 的指针"，
 *      想把它存进 trapframe 里，得先找个不冲突的地方落脚。
 *      初始值先设 0，表示"现在跑的是内核代码"。
 */
void idt_init(void) {

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}//我的代码打印方式是%016lx，但是这里却是08x（当做一个unsinged int读），只读32位而非64，因为测试数据给的cause = 20，很小，32位足够

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

//lab3 function2 2510538
// 张铭瑞负责：exception_handler() 与 trap() 分发逻辑；邓翔予(2514022)负责：interrupt_handler()。

/* insn_len - 一条指令占几个字节（2 还是 4），决定 epc 该往前挪多少
 *
 * 为什么需要它：
 *   RISC-V 的指令有长有短。常见的是 4 字节，但编译器为了省空间，会把
 *   高频指令压成 2 字节的"压缩指令"。异常处理完之后要让程序从"下一条"
 *   接着跑，就必须知道当前这条有多长 —— 一律加 4，遇到 2 字节指令就会
 *   跳过一半的下一条指令。
 *
 * 怎么分辨（不用完整解码，只看一个标记位）：
 *   32 位指令的机器码最后两位固定是二进制的 11，这是 RISC-V 规范
 *   故意留的识别标记；2 字节压缩指令的最后两位一定不是 11。
 *   所以只要读出这个地址处的低 16 位，看最低 2 位就够了。
 *
 *   类比：看身份证最后一位判性别，不用把整个号码读完。
 *
 * 本实验只涉及 16 位和 32 位两种；更长的编码（48 位以上）不在范围内。
 */

/*

trapframe 在栈上
   │
   ├─ tf->cause = 3
   ├─ tf->epc   = ebreak 的地址    tf->epc 是内存里的字段；C 函数只改内存里的 tf->epc
   │
你的 exception_handler:
   ├─ 打印 "Breakpoint at 0x..."
   └─ tf->epc += 4
   │
返回 trap() → 返回 __trapret
   │
RESTORE_ALL:
   ├─ 返回代码把 tf->epc 写回 sepc寄存器 
   └─ 恢复其他寄存器
   │
sret:       sret 再用 sepc 跳回去。
   └─ PC = sepc     sepc 是 CPU 里的寄存器；
   │
CPU 从新地址继续执行


为什么不在 C 里直接改 sepc？
可以，但没必要，而且和框架设计不一致。框架已经把现场保存成 trapframe，返回代码统一从 trapframe 恢复；你在 C 里改 tf->epc 最自然，也不会破坏“汇编保存/恢复”的一致性。

*/

//为什么不用c库标准的printf？因为printf最终要调用内核的write，但是我们写的就是内核，内核编译时用了 -nostdinc，没有标准 C 库，内核也没有“用户态/系统调用”这一层，串口是硬件，直接操作需要 M 态权限
//所以内核自己实现了一个 cprintf，它不依赖 libc，而是通过 SBI 让 M 态的 OpenSBI 帮忙写串口。
static inline size_t insn_len(uintptr_t epc) {//size_t是无符号整数，返回2或者4都可以
    return (*(const uint16_t *)(uintptr_t)epc & 0x3) == 0x3 ? 4 : 2;  //0x3的二进制是11，只读最低位也就是前两个字节，如果是11就返回4，否则返回2
    //因为标准指令长度4字节，最低2位是11；压缩指令长度2字节，最低2位不是11
}
//uinptr_t是装得下一个指针的无符号整数，在本项目中是64位

/* exception_handler - 异常处理（非法指令 / 断点 / 其他兜底）
 *
 * 异常和中断的区别（最容易被追问的一点）：
 *   中断是"外面的事"——时钟到点了、外部设备来数据了，跟当前正在执行
 *     哪条指令没关系，只是恰好赶上；
 *   异常是"这条指令本身有问题"——执行了不存在的指令、主动下了个断点、
 *     访存越界。
 *   共同点是：两者都要让内核暂停当前流程、处理一下、再原样回来。
 *
 * epc 是什么（理解这个函数的关键）：
 *   它是硬件记下的"被打断那一条指令的地址"，相当于夹在书里的书签。
 *   sret 会跳回 epc 继续执行。
 *   对异常来说，出问题的那条指令如果原封不动再执行一次，必然再炸一次，
 *   于是 sret 回来 -> 又异常 -> 又回来，死循环。
 *   所以处理完必须把 epc 往后挪，跳过这条"肇事指令"，
 *   这才是"这个异常已经被处理掉了"的真正含义。
 *
 * 函数签名由实验框架规定；框架测试代码以 extern 方式引用，因此不能加 static。
 *
 * 输出格式注意：tools/grade.sh 用固定字符串做哨兵匹配，
 * 必须是 "Illegal instruction at" 和 "Breakpoint at"，换措辞就通不过比对。
 */
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {//cause是原因码
    case CAUSE_ILLEGAL_INSTRUCTION://宏定义，实际上就是2和3，这样做便于阅读
        // 非法指令：CPU 说不认识这串机器码
        cprintf("Illegal instruction at 0x%016lx\n", tf->epc);//lab1说过的crprintf，这里的意思是64位16进制打印，宽度16，不足补0
        // 跳过这条指令，否则 sret 回来会再触发同一异常，形成死循环
        tf->epc += insn_len(tf->epc);//必须先打印再改，不能调换顺序
        break;

    case CAUSE_BREAKPOINT:
        // 断点：程序里主动写了 ebreak，相当于"在这里停一下"
        cprintf("Breakpoint at 0x%016lx\n", tf->epc);
        tf->epc += insn_len(tf->epc);
        break;

    default:
        // 实验未要求具体处理的异常：打印完整 trapframe 兜底，不推进 epc。
        // 这里故意不往后挪 —— 不认识的异常往往根本恢复不了，
        // 硬把 epc 加过去等于把"没治好"伪装成"已治好"，反而掩盖问题。
        // 未处理的其他异常包括：缺页，访问错误，未知cause
        print_trapframe(tf);
        break;
    }
}
//四组测试数据：
/*1-3为工程自带的，4是我自己加的。
124都会走真实的trap入口，而3会在栈上创造一个假的trapframe直接调用函数
1只测试4字节的指令，没有测试2字节的，因此4专门测试2字节
1测试的是上下文和断点（中断），主要验证入口保存+返回恢复+=4，需要检查26个寄存器；2测试的是非法指令+=4返回，不需要检查寄存器，测试内容为故意往代码里塞四个字节的非法内容，不验证断点
3测试未知异常的兜底功能，直接打印错误，不需要检查寄存器；4测试2字节指令即+2分支，不需要检查寄存器，只看after是否打印
*/



//lab3 function2 2514022
// 邓翔予负责：interrupt_handler()（时钟中断 / 软件中断 / 外部中断 / 未知中断兜底）。

/* ticks_reported - 已经打印过多少次统计信息
 *
 * 纯粹是给人用的"自动关机开关"。正常 make qemu 跑起来后内核会每秒
 * 刷一行 "100 ticks"，不关机就永远刷下去。这里数够 SHUTDOWN_AFTER 次
 * 就主动关机，省得每次都要手动 Ctrl+A X 退 QEMU。
 * 静态变量默认 0，不需要额外初始化。
 */
static int ticks_reported = 0;

/* interrupt_handler - 中断处理
 *
 * 第一步永远是"剥离标志位"：
 *   scause 的最高位（第 63 位）是标志位，1 表示这是中断，0 表示这是异常，
 *   真正的类型码在低 63 位。
 *   所以必须先把这个标志位清掉再和 IRQ_S_TIMER 比 ——
 *   直接拿 tf->cause 去比，高位一直是 1，比较永远不成立。
 *   （trap_dispatch 正是靠这一位分流把我们送到这里的，进来时它一定是 1。）
 *
 * 输出字符串受 tools/grade.sh 的哨兵约束（用 grep -F 精确比对）：
 *   IRQ_S_SOFT  必须打出 "Supervisor software interrupt"
 *   未知原因码  必须打出 "Unknown interrupt: cause <原因码>"
 * 措辞改一个字，Test 2 就会判失败。
 */
void interrupt_handler(struct trapframe *tf) {
    // 清掉最高位的中断标志，得到纯"原因码"
    uintptr_t irq = tf->cause & ~((uintptr_t)1 << 63);

    if (irq == IRQ_S_TIMER) {
        /* 时钟中断：第一件事永远是"再定一个闹钟"。
         *
         * SBI 的时钟事件是一次性的：设一次、响一次，响完就作废，
         * 不像手机闹钟能设"每天重复"。所以每进一次时钟中断，
         * 都必须重新调一次 clock_set_next_event()。
         *
         * 漏掉的后果特别典型：中断只来一次，ticks 停在 1，
         * 之后内核安安静静什么都不发生 —— 靠 ticks 推进的等待循环
         * 会永远等下去，直到超时。这正是 Test 3 最容易踩的坑。
         */
        clock_set_next_event();

        // 全局计数器，只在 clock_init() 里清过一次零
        ticks++;

        /* 每 TICK_NUM 次中断报一行。
         * QEMU 的 timebase = 100000，意思是 1 秒被切成 10 万份；
         * 每次中断间隔 10 万份 = 1/100 秒，凑满 100 次正好 1 秒。
         * 所以 "100 ticks" 这句话的言外之意是"又过去 1 秒了"。 */
        if (ticks % TICK_NUM == 0) {
            print_ticks();

            // 刷够 SHUTDOWN_AFTER 行就关机，见上面 ticks_reported 的说明
            ticks_reported++;
            if (ticks_reported >= SHUTDOWN_AFTER) {
                sbi_shutdown();
            }
        }
    } else if (irq == IRQ_S_SOFT) {
        // 软件中断：本实验没有实际触发，保留一条可读的输出便于区分
        cprintf("Supervisor software interrupt\n");
    } else if (irq == IRQ_S_EXT) {
        // 外部中断：同样没有实际触发，外部设备走这条
        cprintf("Supervisor external interrupt\n");
    } else {
        /* 未知原因码：先报出码值，再把整个 trapframe 摊开，然后正常返回。
         *
         * 这里刻意不 panic：来一个不认识的硬件中断就让整个内核死掉，
         * 是过度反应。报出来、把现场留下来、继续跑，更符合内核对
         * 未知事件的处理姿态（测试也要求这里只打印不崩）。 */
        cprintf("Unknown interrupt: cause %ld\n", (intptr_t)irq);
        print_trapframe(tf);
    }
}


/* trap_dispatch - 一级分流：这条 trap 是中断还是异常
 *
 * 判断手法很巧：scause 最高位（第 63 位）在 RISC-V 规范里就是中断标志。
 * 把它当"有符号整数"来读，最高位是 1 的值就是负数，
 * 于是 (intptr_t)tf->cause < 0 等价于"这是中断"。
 * 一行比较就完成分流，不用先取位再比。
 *
 * 注意这是"一级分流"，只分大类；具体是哪一种中断/异常，
 * 由两个 handler 内部再看原因码细分（二级分流）。
 */
static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // 最高位为 1：中断（外面的事，比如时钟到点）
        interrupt_handler(tf);
    } else {
        // 最高位为 0：异常（当前这条指令自己的问题）
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}
