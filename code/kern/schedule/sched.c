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
所以它当 current 时只能从表头开始找；用 &current->list_link 会顺一个不在链表里的节点走野指针。
为什么要兜底 idleproc：所有线程都可能正在睡，此时若 next 为 NULL，后面 next->runs++ 直接空指针崩溃；
idleproc 永远可运行，用它保证“CPU 永远有活干”。*/

/* ---- 答辩问答（对应本函数） ----
 * Q1：第一句 current->need_resched = 0 是干什么的？删掉会怎样？
 * A ：清掉“本次调度请求”。不清的话，线程刚被切回来又看到请求，立刻再切走，
 *     表现为空转：runs 疯涨、几乎没有线程真正执行。
 * Q2：last 为什么要分两种取值？
 * A ：idleproc 是复用启动现场造的 0 号空闲线程，没有挂进 proc_list，它的 list_link
 *     是孤立节点；从它往后找会走野指针。普通线程从自己节点的下一个开始，才是公平轮转。
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
        le = last;
        do {
            if ((le = list_next(le)) != &proc_list) {
                next = le2proc(le, list_link); /* 链表节点 → PCB */
                if (next->state == PROC_RUNNABLE) {
                    break; /* 找到一个可运行的，就用它 */
                }
            }
        } while (le != last); /* 转一圈回到起点就停 */

        /* 一个可运行线程都没有（或者找到的那个状态已变）就退回 idleproc。
         * idleproc 永远可运行，保证系统不会“没有线程可跑”。 */
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }

        next->runs++;          /* 统计该线程被调度的次数 */
        if (next != current) {
            proc_run(next);    /* 真正切换：改 current、切页表、switch_to */
        }
    }
    local_intr_restore(intr_flag);
}


