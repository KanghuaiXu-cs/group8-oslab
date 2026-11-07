#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

/* =============================================================================
 * sync.h —— 内核同步原语（lab3 阶段只提供"关中断临界区"这一种）
 *
 * 【解决什么问题】内核里有大量全局共享数据（如 pmm.c 的空闲页链表）。如果
 *   一段代码正读到一半被时钟中断打断、中断处理里又去动同一份数据，就会出现
 *   丢数据/数据结构损坏。最简单可靠的保护办法：进临界区前关中断，出去时恢复。
 *
 * 【为什么是"保存-恢复"而不是无脑关/开】
 *   嵌套场景：A 函数已经关了中断，B 函数又调用 local_intr_save——此时中断
 *   本来就是关的，如果 B 结束时无脑开中断，就把 A 的保护拆掉了。
 *   正确做法是记住"进临界区之前中断本来就开没开"（flag），退出时恢复原状：
 *     - 进之前开着（flag=1）→ 出来时要重新打开
 *     - 进之前已关（flag=0）→ 出来时保持关闭
 *   这就是 flag 参数的全部意义。
 * ============================================================================= */

#include <defs.h>
#include <intr.h>
#include <riscv.h>

/* __intr_save - 关中断前的快照动作
 * 消费点：读 sstatus 的 SIE 位（bit5，S 模式全局中断使能）判断当前状态
 * 副作用：如果原本开着，就把它关掉
 * 返回值：1 = "原来开着，我把它关了，恢复时要负责重新打开"
 *         0 = "原来就是关的，我什么都没动，恢复时保持关闭" */
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

/* __intr_restore - 按快照恢复
 * 只有 flag=1（说明是本层关的）才重新打开；flag=0 时什么都不做，
 * 绝不能无脑 intr_enable()——那会误开别人的临界区。 */
static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

/* local_intr_save / local_intr_restore - 对外使用的宏对（成对出现，缺一不可）
 *
 * 用法模板（见 pmm.c 的 alloc_pages/free_pages/nr_free_pages）：
 *     bool intr_flag;
 *     local_intr_save(intr_flag);
 *     {   ...访问全局共享数据的临界区...  }
 *     local_intr_restore(intr_flag);
 *
 * 实现细节：
 *   - do { } while (0)：让宏在 if/else 等任何语句环境下都表现为"一条完整
 *     语句"，避免宏展开后被就近的 else 错误配对（C 宏的经典防御写法）。
 *   - intr_flag 由调用者声明并传入，两个宏通过它传递快照值。 */
#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */
