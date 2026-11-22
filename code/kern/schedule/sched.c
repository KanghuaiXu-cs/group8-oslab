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


