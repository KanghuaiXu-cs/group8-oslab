#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2510789
//全局问题：schedule 到底负责什么？
/*调度的本质是“在多个就绪线程里挑一个”。本实验用最简单的轮转（round-robin）：
从当前线程的下一个位置开始沿 proc_list 找，找到第一个 PROC_RUNNABLE 就用它，找不到就退回 idleproc。
选择“谁”是策略，怎么“换过去”是机制，schedule 只负责前者，具体切换交给 proc_run。
为什么起点要分情况：idleproc 是复用启动现场造的，没有挂进 proc_list，
它自己的 list_link 是“指向自己”的孤立节点（list_init 之后 prev=next=自己）。
所以它当 current 时只能从表头开始找；若统一写成 &current->list_link，
list_next(idle) 还是 idle 自己，于是每次都“选中自己”，永远切不到别人（实测：内核卡在
cpu_idle 空转、只剩时钟中断在打印 100 ticks，initproc 一直不出现）。
为什么要兜底 idleproc：所有线程都可能正在睡，此时若 next 为 NULL，后面 next->runs++ 直接空指针崩溃；
idleproc 永远可运行，用它保证“CPU 永远有活干”。*/

/* ---- 答辩问答（对应本函数） ----
 * Q1：第一句 current->need_resched = 0 是干什么的？删掉会怎样？
 * A ：清掉“本次调度请求”。不清的话，线程刚被切回来又看到请求，立刻再切走，
 *     表现为空转：runs 疯涨、几乎没有线程真正执行。
 * Q2：last 为什么要分两种取值？
 * A ：idleproc 是复用启动现场造的 0 号空闲线程，没有挂进 proc_list，它的 list_link
 *     是“指向自己”的孤立节点（prev=next=自己）。若统一用 &current->list_link，
 *     list_next(idle) 返回的还是 idle 自己，于是每次都选中自己、永远不切走
 *     （实测现象：卡在 cpu_idle 空转，只有时钟中断在刷 100 ticks，initproc 不出现）。
 *     普通线程从自己节点的下一个开始，才是公平轮转。
 * Q3：循环结束后为什么还要判 state？
 * A ：do-while 正常走完一圈时，next 是“最后一个看过、但不可运行”的线程，不是 NULL；
 *     只判 NULL 会把一个在睡的线程切上 CPU。所以两种都要兜回 idleproc。
 * Q4：不用 do-while 用 while 行不行？
 * A ：行不通。链表里可能只有当前线程自己，while 可能一次都不进循环，next 永远是 NULL。
 *     do-while 保证“至少检查一次”。*/

/* schedule - 选一个可运行的线程并把 CPU 交给它
 *
 * 分三步：
 *   1. 关中断（选线程 + 切页表 + 切上下文必须原子完成）
 *   2. 从 proc_list 里轮转地找第一个 PROC_RUNNABLE 的线程；找不到就退回 idleproc
 *   3. 调用 proc_run() 真正切换，然后恢复中断
 *
 * 为什么必须关中断：如果选完线程之后、真正切换之前来了一次时钟中断，
 * 中断里的 schedule() 可能又改掉 current 或触发另一次切换，
 * 导致 current 与正在跑的现场不一致。 */
void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    {
        /* 本次调度请求已经处理，先清标志，
         * 否则刚切回来又会立刻被调度出去，形成“空转” */
        current->need_resched = 0;

        /* 从“当前线程的下一个位置”开始找，实现轮转（round-robin）。
         * idleproc 不在 proc_list 里（它是直接复用启动现场的），
         * 所以当前是 idleproc 时，从表头开始找。 */
        last = (current == idleproc) ? &proc_list : &(current->list_link);

        /* ---- 沿环形链表走一圈，挑第一个能跑的线程 ----
         * 链表是环：表头 proc_list 只是“门牌/哨兵”，不是线程；
         * 所以没有 NULL 结尾，走到它要跳过，转回锚点 last 才算走满一圈。
         *   last：锚点，既是起点也是终点
         *   le  ：游标，当前走到哪个节点
         *   next：结果，找到的那个线程
         *
         * 手工走一遍（环：表头 H → P1 → P2 → P3 → H，current = P2，故 last = &P2；
         *             假设 P3 在睡、P1 能跑）：
         *   第 1 轮：le = next(P2) = P3，不是表头 → next = P3，P3 在睡，不 break
         *   第 2 轮：le = next(P3) = H，是表头 → 跳过 if 体；H ≠ P2，继续转
         *   第 3 轮：le = next(H) = P1，不是表头 → next = P1，能跑 → break
         *   结果 next = P1。
         *
         * 注意两点：
         *   1) 它不检查 current 自己（起点是它的下一个），这是“轮转”的语义；
         *   2) 若一圈都没找到，next 会停在“最后看过但不可用”的那个（或仍是 NULL），
         *      交给下面的兜底处理。 */
        le = last;                       /* 游标放到锚点，do-while 会从它的下一个开始 */
        do {
            if ((le = list_next(le)) != &proc_list) {  /* 先走一步；表头是哨兵，跳过 */
                next = le2proc(le, list_link);         /* 节点地址 → PCB 地址（container_of） */
                if (next->state == PROC_RUNNABLE) {
                    break;                             /* 第一个能跑的就用它，不比较优先级 */
                }
            }
        } while (le != last);            /* 转回锚点 = 走满一圈，停 */

        /* ---- 兜底：保证 next 一定“能跑” ----
         * 两种情况都要接住：
         *   1) next == NULL      —— 一圈里一个都没进过 if（链表里没有别的线程）
         *   2) state != RUNNABLE —— 循环正常走完，next 停在“看过但不可用”的那个（不是 NULL）
         * 兜底对象只能是 idleproc：它永远 PROC_RUNNABLE，保证 CPU 永远有活干。
         * 不做兜底：next 为 NULL 时下一行 runs++ 直接空指针崩；next 在睡时会被强行切上 CPU。 */
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }

        /* ---- 记账 + 真正切换 ----
         * runs++ 统计“被选中次数”，不参与决策；选中自己也会加一。
         * 顺序不能反：runs++ 要解引用 next，必须在兜底之后；
         * 而 proc_run 一旦切走，本函数要等这个线程将来被切回来，才会从下一行继续。 */
        next->runs++;
        if (next != current) {   /* 选中自己就不用切：存一遍自己的现场再取一遍，纯浪费 */
            proc_run(next);      /* 改 current、切 satp 并刷 TLB、switch_to */
        }
    }
    local_intr_restore(intr_flag);
}


