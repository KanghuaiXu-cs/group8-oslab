# Lab4 提示词套件

> 使用顺序：每个模块内部都按「主实现 → 测试生成 → 代码审查」使用；如果审查发现问题，用审查的输出回到主实现的 `[SPECIFICATION]` 补约束。
>
> 本套件覆盖三个功能模块：
> - 功能模块一（虚拟内存页操作，2514022-邓翔予）：第 6 节
> - 功能模块二（进程创建及初始化，2510538-张铭瑞）：第 7 节
> - 功能模块三（进程调度，2510789-徐康怀）：第 1–5 节
>
> 第 1–5 节保持徐康怀原稿的写法；第 6–7 节按《提示词模板》的 `[PROMPT]` 三字段（任务 / 操作要求 / 输出要求）与 `[SPECIFICATION]` 四要素（Pre-Condition / Post-Condition / Case / Requirements）补齐。

---

## 1. 主实现提示词（功能模块三：进程调度）

````markdown
[PROMPT]
在 riscv64-ucore 的 lab4 中实现“功能模块三：进程调度”，只改三处标记：
1. kern/process/proc.c 的 //LAB4 function3 2510789：cpu_idle()、proc_run()
2. kern/schedule/sched.c 的 //LAB4 function3 2510789：schedule()
3. kern/process/switch.S 的 //LAB4 function3 2510789：switch_to()
要求给出完整代码、逐段解释（解释“为什么”而不只是“做什么”），以及可执行的验证方法。
不要修改这三个文件里的其他函数，不要改 Makefile 和 tools/。

[RELY]
- struct context 定义在 kern/process/proc.h，字段顺序：
  ra, sp, s0, s1, ..., s11（共 14 个 uintptr_t，偏移 0..13*8）
- struct proc_struct 关键字段：
  state（PROC_UNINIT/PROC_SLEEPING/PROC_RUNNABLE/PROC_ZOMBIE）、
  kstack、need_resched、context、tf、pgdir（页目录物理地址）、list_link
- 全局量：proc_list（链表头）、idleproc、initproc、current
- 机制：
  * idleproc 直接复用启动现场，kstack = bootstack，且**不在 proc_list 里**；
  * copy_thread 把新线程的 context.ra 设为 forkret、context.sp 设为它的 trapframe，
    tf->epc = kernel_thread_entry，tf->gpr.s0 = fn，tf->gpr.s1 = arg；
  * 因此 switch_to 的 ret 会进入 forkret → forkrets → __trapret → sret → kernel_thread_entry；
  * proc->pgdir 是页目录的**物理地址**，satp 要的是物理页号（要右移 12）。
- 已有接口：local_intr_save/local_intr_restore（kern/sync/sync.h）、
  write_csr/read_csr、SATP_MODE_SV39 = 8、RISCV_PGSHIFT = 12（libs/riscv.h）、
  flush_tlb()（kern/mm/pmm.h，就是 sfence.vma）、le2proc()、list_next()
- 反面教材：libs/riscv.h 里的 lsatp() 是 Sv32 遗留写法（用 SATP32_MODE），
  表达不了 Sv39，禁止使用；正确写法参照 kern/mm/pmm.c 的 enable_paging()。
- 编译选项 -Wall -Werror，不能有警告。

[GUARANTEE]
- 交付四个函数，签名不可改：
  void cpu_idle(void); void schedule(void);
  void proc_run(struct proc_struct *proc); void switch_to(struct context *, struct context *);
- switch_to 只保存/恢复 ra、sp、s0~s11，最后 ret；
- schedule 与 proc_run 的“选择 + 切换”必须在关中断的临界区内完成；
- proc_run 必须写 Sv39 的 satp 并在之后 flush_tlb()；
- schedule 找不到可运行线程时必须退回 idleproc；
- cpu_idle 是死循环（proc.h 里声明了 noreturn）；
- 三处 //LAB4 function3 标记必须保留，并把 YOUR CODE 换成学号。

[SPECIFICATION]
一、switch_to(from=a0, to=a1)：
  1. 把当前 ra、sp、s0~s11 存进 from->context 的对应偏移；
  2. 从 to->context 恢复同样 14 个寄存器；
  3. ret 跳到恢复出的 ra。
  边界：存取基址是 a0/a1，不是 sp，所以 sp 不需要“最后恢复”；
        对新建线程，to->context.ra = forkret，这是它第一次运行的入口。

二、schedule()：
  1. local_intr_save 关中断；
  2. current->need_resched = 0；
  3. 起点 last = (current == idleproc) ? &proc_list : &(current->list_link)；
  4. 从 last 开始用 list_next 轮转，跳过表头，遇到第一个 state == PROC_RUNNABLE 的 break；
  5. 转完一圈没找到（或找到的已不可运行）→ next = idleproc；
  6. next->runs++；若 next != current 则 proc_run(next)；
  7. local_intr_restore 恢复中断。
  边界：idleproc 不在 proc_list 里，所以起点必须分情况；
        一个 RUNNABLE 都没有时要回退 idleproc，不能返回 NULL。

三、proc_run(proc)：
  1. if (proc == current) 直接返回；
  2. 关中断；
  3. current = proc；
  4. write_csr(satp, ((uintptr_t)SATP_MODE_SV39 << 60) | (proc->pgdir >> RISCV_PGSHIFT))；
  5. flush_tlb()；
  6. switch_to(&prev->context, &proc->context)；
  7. 恢复中断。
  边界：不能调用 lsatp()（Sv32 写法）；换页表后必须刷 TLB。

四、cpu_idle()：
  死循环里 if (current->need_resched) schedule();

验收：
1. make 无 undefined reference；
2. 注入测试后打印 [Test Schedule] PASS；
3. initproc 能打印 this initproc, pid = 1, name = "init" 和 To U: "Hello world!!".；
4. 连续多次运行结果稳定。
````

### 这份提示词的关键点

| 段落 | 为什么这么写 |
|---|---|
| `[PROMPT]` | 精确到“文件 + 标记 + 函数”，避免 AI 顺手改掉模块一/模块二的代码 |
| `[RELY]` | 把 `struct context` 字段顺序、`copy_thread` 设的“路标”、`pgdir` 是物理地址、`lsatp()` 是 Sv32 陷阱这些**真实事实**喂给 AI，否则它会按“普通过程调用”瞎猜 |
| `[GUARANTEE]` | 把“只存 14 个寄存器”“必须关中断”“必须 flush_tlb”“必须回退 idleproc”写成硬约束 |
| `[SPECIFICATION]` | 每个函数写成可执行步骤 + 边界条件，尤其 `schedule` 的起点分情况、`proc_run` 的 satp 拼法 |

---

## 2. 测试生成提示词（功能模块三）

````markdown
[PROMPT]
针对已经实现的 schedule() / proc_run() / switch_to() / cpu_idle()，设计并生成验证方案，用来证明：
1. initproc 能在第一次调度中被真正切换进去并执行（首次运行链路 forkret → forkrets → __trapret → sret → kernel_thread_entry 通）；
2. 多个线程之间能**来回**切换（切走之后还能切回来）；
3. schedule() 在没有可运行线程时会退回 idleproc，不会卡死。
要求给出测试代码、插入位置、预期输出与判定方法；测试代码不能混进最终交付。

[RELY]
- 已有测试框架：tools/test_lab4.c（含 test_lab4_pmm_basic / test_lab4_process_create /
  test_lab4_schedule）、tools/inject.py（把三个测试分别注入 pmm_init 末尾、
  proc_init 末尾、init_main 开头）；
- kernel_thread() 可以直接创建内核线程；
- current->need_resched = 1; schedule(); 可以主动触发一次调度；
- 断言用 assert()（失败会 panic 并打印）；
- 输出用 cprintf；
- 反汇编用 riscv64-unknown-elf-objdump -d bin/kernel，符号用 nm。

[GUARANTEE]
- 不修改 tools/ 下的框架测试；
- 临时验证代码只在临时副本里，验证后删除，不混进最终提交；
- 给出每一步的预期现象和失败时的现象；
- 明确写出没有覆盖的场景（抢占式调度、时间片、用户态）。

[SPECIFICATION]
测试 1（首次运行）：
- 依赖注入到 init_main 开头的 test_lab4_schedule()；
- 预期：current == idleproc 时 schedule() 选中 initproc，
  switch_to 后经 forkret/forkrets/__trapret/sret 进入 kernel_thread_entry，
  最终执行 init_main，打印 this initproc, pid = 1, name = "init"；
- 失败现象：卡在 cpu_idle 空转（schedule 没选中 initproc）、
  或 panic（context.ra / tf->epc 没设对）。

测试 2（来回切换）：
- 用 kernel_thread 创建两个线程，函数体里循环 current->need_resched = 1; schedule();
- 主线程里连续调用 8 次 schedule()；
- 预期：断言两个线程都执行过（各自的 volatile 标志被置 1），打印 [Test Schedule] PASS；
- 失败现象：只有一个线程跑过（轮转起点错）、
  或 runs 疯涨但没有任何线程真正执行（need_resched 没清零）、
  或 assert 失败（switch_to 漏存某个 callee-saved 寄存器）。

测试 3（回退 idleproc）：
- 把所有线程都置成非 RUNNABLE（或只留 idleproc），调用 schedule()；
- 预期：next == idleproc，系统继续运行（不 panic、不死锁）。

覆盖说明：
- 没有覆盖抢占式调度（时钟中断里强制 schedule）、时间片、
  用户态进程、多 hart 调度。
````

---

## 3. 代码审查 / 修 bug 提示词（功能模块三）

````markdown
[PROMPT]
以代码审查者的身份，逐条检查 switch_to()、schedule()、proc_run()、cpu_idle() 是否满足实验约束。
不要重写代码；按“问题、位置、原因、修法、验证”输出表格。重点检查以下七项：
1. switch_to 是否只保存/恢复 ra、sp、s0~s11（不多不少），最后是否 ret；
2. schedule / proc_run 的临界区是否覆盖了“选择 + 切换”；
3. schedule 是否清 current->need_resched；
4. schedule 的轮转起点是否正确处理 current == idleproc；
5. 是否在找不到 RUNNABLE 时回退 idleproc；
6. proc_run 的 satp 是否是 Sv39 写法（不是 lsatp / SATP32_MODE），是否 flush_tlb；
7. 是否误改了模块一/模块二的文件（kern/mm/pmm.c、模块二的函数）或 Makefile/tools/。

[RELY]
- 实验要求：机制与策略分离；switch_to 只做现场保存/恢复；
- struct context 字段顺序：ra, sp, s0..s11；
- 测试 sentinel："[Test Schedule] PASS"、this initproc, pid = 1, name = "init"、
  To U: "Hello world!!".；
- 编译选项 -Wall -Werror；
- 课程环境 QEMU 4.1.1，本地验证环境可能不同。

[GUARANTEE]
- 输出问题表，每条包含：文件/行、现象、原因、建议修改、验证方法；
- 只给修改建议，不直接动无关代码；
- 若代码没有问题，明确写“七项均通过”，不要为了凑问题硬编。

[SPECIFICATION]
逐项判定：
1. 漏存某个 s 寄存器 → 线程切回来时该寄存器是别人的值，现象是“偶发跑飞”；
   多存/少存都不影响正确性，但保存全部 32 个通用寄存器是多余的（有调用约定兜底）；
2. 临界区不完整 → 选完线程后若来中断，current 可能与实际现场不一致；
3. 忘清 need_resched → 刚切回来又被切走，runs 疯涨；
4. 起点无条件用 &proc_list → idleproc 永远优先；无条件用 &current->list_link → idleproc 时野指针；
5. 不回退 idleproc → next 为 NULL 时解引用崩溃，或没有线程可跑时死锁；
6. 用 lsatp() → satp 的 MODE 字段错误（0x80000000 是 Sv32 的位置），页表直接失效；
   不 flush_tlb → 可能命中旧翻译；
7. 改了 pmm.c / 模块二函数 / Makefile / tools → 违反分工与实验约束。
````

---

## 4. 模块三这三份提示词分别解决什么问题

| 提示词 | 解决的问题 | 对应最终代码 / 测试 |
|---|---|---|
| 主实现 | 让 AI 生成正确的四个函数（尤其是 satp 拼法与轮转起点） | `kern/process/proc.c`、`kern/schedule/sched.c`、`kern/process/switch.S` |
| 测试生成 | 证明“首次运行”“来回切换”“回退 idleproc”三条路径 | 5.3 调度测试 |
| 代码审查 | 抓漏存寄存器、临界区不完整、need_resched 未清、satp 写法等坑 | 4.3 的迭代记录 |

---

## 5. 拆分提示词时的一个原则

不要把“实现”和“验证”混在一个提示词里。

- 主实现提示词只负责“写对”；
- 测试提示词只负责“证明它对”；
- 审查提示词只负责“找它哪里可能不对”。

三份分开，AI 不容易在一个提示词里顾此失彼；自己定位问题时，也能更快分清是“实现错了”“测试错了”还是“环境不一样”。

---

## 6. 功能模块一：虚拟内存页操作（2514022-邓翔予）

### 6.1 主实现提示词

````markdown
[PROMPT]
**任务**：修改 `kern/mm/pmm.c`，实现功能模块一「虚拟内存页操作」：在现有 Sv39 三级页表上建立/更新「线性地址 → 物理页」的映射，并幂等撤销映射。只改 `page_remove_pte()` / `page_insert()` / `page_remove()` 三个函数（原骨架 `//LAB4 function1` 标记处）。这是 `pmm_init()` 内 `check_pgdir()` / `check_boot_pgdir()` 断言、以及后续线程地址空间逻辑的共同基础。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留文件中的现有代码（特别是 pmm.c 里已回填的 lab2 物理内存管理代码，禁止整体覆盖）；允许新增 static 辅助函数；不得改动 `get_pte()`、`tlb_invalidate()`、`check_pgdir()`、`check_boot_pgdir()` 等既有函数，不得修改任何头文件。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，以工程真实代码为准；`pte_create()` 已自动带上 `PTE_V`，不要再重复拼；错误码用 `libs/error.h` 的 `-E_NO_MEM`。

[RELY]
// 待实现函数的声明（kern/mm/pmm.h:44-49）
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);          // pmm.c:229 已实现
void page_remove(pde_t *pgdir, uintptr_t la);                     // pmm.h:46
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la,
                uint32_t perm);                                   // pmm.h:47
void tlb_invalidate(pde_t *pgdir, uintptr_t la);                  // pmm.h:49；pmm.c:349

// 关键宏 / 内联函数（kern/mm/pmm.h，从源码复制）
#define alloc_page() alloc_pages(1)                               // :41
#define free_page(page) free_pages(page, 1)                       // :42
static inline struct Page *pte2page(pte_t pte);                   // :122-130，PTE_V=0 时会 panic
static inline int page_ref(struct Page *page);                    // :138
static inline void set_page_ref(struct Page *page, int val);      // :144
static inline int page_ref_inc(struct Page *page);                // :151
static inline int page_ref_dec(struct Page *page);                // :158，返回递减后的值
static inline void flush_tlb() { asm volatile("sfence.vma"); }   // :164
static inline pte_t pte_create(uintptr_t ppn, int type)           // :170
{ return (ppn << PTE_PPN_SHIFT) | PTE_V | type; }                 // PTE_V 自动带上

// 权限位与地址索引（kern/mm/mmu.h）
#define PTE_V 0x001                                               // :62
#define PTE_R 0x002                                               // :63
#define PTE_W 0x004                                               // :64
#define PTE_U 0x010                                               // :66
#define PGSIZE 4096                                               // :51
#define PTE_PPN_SHIFT 10                                          // :59
#define PTXSHIFT 12                                               // :56
#define PDX0SHIFT 21                                              // :57
#define PDX1SHIFT 30                                              // :58
#define PDX1(la) ((((uintptr_t)(la)) >> PDX1SHIFT) & 0x1FF)       // :28
#define PDX0(la) ((((uintptr_t)(la)) >> PDX0SHIFT) & 0x1FF)       // :29
#define PTX(la)  ((((uintptr_t)(la)) >> PTXSHIFT) & 0x1FF)        // :32

// 错误码与全局量
#define E_NO_MEM 4                                                // libs/error.h:8
extern struct Page *pages;                                        // pmm.c:18-29
extern pde_t *boot_pgdir_va;

[GUARANTEE]
必须实现或修改的接口（同一功能模块，可加 static 辅助函数）：
```c
// 允许的 static 辅助：撤销单条已确认有效的叶子 PTE
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep);
// 必须实现
int  page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```
不要求实现 `pgdir_alloc_page()` / `print_pgdir()`（本实验未调用）；不得修改头文件与其他模块的文件。

[SPECIFICATION]
## page_insert
**Pre-Condition**:
- `pgdir` 是可用的页目录基址（内核虚拟地址），`page` 是已分配的物理页；
- `la` 可以已映射，也可以未映射；
- `perm` 是调用者期望的低位权限，不必包含 `PTE_V`。

**Post-Condition**:
- 成功返回 0：`get_pte(pgdir, la, 0)` 得到的叶子 PTE 有效，`pte2page(*ptep) == page`，权限恰为 `perm | PTE_V`；
- `page_ref(page)` 反映「新增一次映射」；
- 该地址对应的 TLB 项已失效。

  **Case 1（la 原本未映射）**:
  - `get_pte(pgdir, la, 1)` 按需分配中间级页表页；分配成功则建立映射并把 `page_ref` 加一。

  **Case 2（la 已映射到同一个 page）**:
  - 引用计数净增 0，不得释放该页；PTE 权限仍要按本次 `perm` 重写（例如从 `PTE_W` 改成无写权限）。

  **Case 3（la 已映射到另一个物理页 old）**:
  - `old` 引用减一、归零时 `free_page(old)`；`page` 引用加一；新 PTE 权限按 `perm` 设置，旧项的 `PTE_U/PTE_W` 等不得残留。

  **Case 4（get_pte 返回 NULL）**:
  - 返回 `-E_NO_MEM`，不写入叶子 PTE、不改变 `page_ref(page)`，不留下半完成映射。

**Requirements**:
- 必须保持「PTE 内容 / 物理页引用计数 / TLB 状态」三者同步，每次写 PTE 后调用 `tlb_invalidate(pgdir, la)`；
- 必须先给新页加引用，再处理旧映射，否则同页重映射时会把目标页提前释放；
- 不得用 `*ptep |= ...` 拼接权限（会残留旧位），应整项覆盖写；
- `pte2page()` 只能在 `PTE_V` 已置位时调用。

## page_remove
**Pre-Condition**:
- `pgdir` 可用；`la` 可能有映射、可能映射已被删除、也可能连中间级页表页都不存在。

**Post-Condition**:
- 有映射时：清空 PTE、递减（必要时释放）物理页、失效 TLB；
- 无映射时：安静返回，不改变空闲页数；重复调用结果一致。

  **Case 1（有有效映射）**:
  - `get_pte(pgdir, la, 0)` 返回非 NULL 且 `*ptep & PTE_V`，正常删除。

  **Case 2（PTE 无效）**:
  - 返回非 NULL 但项无效（例如刚被删过），安静返回。

  **Case 3（中间级页表不存在）**:
  - `get_pte(pgdir, la, 0)` 返回 NULL，安静返回；不得分配页表页。

**Requirements**:
- 实现为幂等接口，不得把「未映射」当作异常；
- `get_pte` 的 `create` 参数必须为 0；
- `free_page()` 只在 `page_ref_dec()` 返回 0 时调用。

## page_remove_pte（static 辅助）
**Pre-Condition**:
- `ptep` 指向叶子 PTE，调用方已确认 `*ptep & PTE_V`。

**Post-Condition**:
- 引用计数减一；归零时释放物理页；PTE 清零；TLB 项失效。

**Requirements**:
- 只被 `page_insert()` 的换页分支和 `page_remove()` 复用，避免两处重复代码；
- 不得在 `PTE_V=0` 时调用 `pte2page()`。

验收：
1. `make qemu` 依次打印 `check_alloc_page() succeeded!`、`check_pgdir() succeeded!`、`check_boot_pgdir() succeeded!`；
2. 注入测试后打印 `[Test PMM] PASS`；
3. `test_lab4_pmm_basic` 的三组断言全部通过：换页后旧页引用归零、同页重复插入不增引用、`page_remove` 连调两次幂等。
````

### 这份提示词的关键点（模块一）

| 段落 | 为什么这么写 |
|---|---|
| `[PROMPT]` | 精确到「文件 + 三个函数 + 原标记位置」，并明确禁止改 `get_pte` / `check_pgdir`，避免 AI 顺手改掉框架代码 |
| `[RELY]` | 把 `pte_create` 自带 `PTE_V`、`pte2page` 对 V=0 会 panic、`page_ref_dec` 返回递减后的值这些真实语义喂给 AI，避免它重复拼 V 位或先转换后判 V |
| `[GUARANTEE]` | 把 `page_remove_pte` 明确列为允许的 static 辅助，避免 AI 把删除逻辑在 `page_insert` 和 `page_remove` 里各复制一份 |
| `[SPECIFICATION]` | 用 Case 把「未映射 / 同页 / 异页 / 分配失败」四条路径分开，尤其同页重映射和 `-E_NO_MEM` 无副作用，是最容易写错的两处 |

### 6.2 测试生成提示词

````markdown
[PROMPT]
针对已经实现的 page_insert() / page_remove() / page_remove_pte()，设计并生成验证方案，用来证明：
1. 建立映射后 PTE 有效、pte2page(*ptep) 指向目标页、引用计数为 1；
2. 换页时旧页引用归零并被释放，新页引用加一，旧权限位不残留；
3. 同一物理页重复映射到同一地址时引用计数不增、页面不被释放；
4. page_remove() 对同一地址连续调用两次仍安全（幂等），且未映射地址不消耗空闲页。
要求给出测试代码、插入位置、预期输出与判定方法；测试代码不能混进最终交付。

[RELY]
- 已有测试框架：tools/test_lab4.c 的 test_lab4_pmm_basic()，tools/inject.py 会把它注入 pmm_init 末尾；
- 可用接口：alloc_page() / free_page()、page_insert() / page_remove()、get_pte()、pte2page()、page_ref()、nr_free_pages()；
- 断言用 assert()（失败会 panic 并打印），输出用 cprintf；
- 全局页目录是 boot_pgdir_va；测试地址用 0 / PGSIZE / 2*PGSIZE 即可，不要占用内核已映射区域。

[GUARANTEE]
- 不修改 tools/ 下的框架测试；
- 临时验证代码只放临时副本，验证后删除，不混进最终提交；
- 每个用例给出预期现象和失败现象；
- 明确写出没有覆盖的场景（跨 2MiB 大页边界、Sv48、并发映射）。

[SPECIFICATION]
测试 1（建立映射）：
- 分配 p1，page_insert(boot_pgdir_va, p1, 0, 0)；
- 预期：返回 0，get_pte(..., 0) 非空且 *ptep & PTE_V，pte2page(*ptep) == p1，page_ref(p1) == 1；
- 失败现象：返回非 0、PTE 无效、或引用计数不为 1。

测试 2（换页与权限覆盖）：
- 先 page_insert(p2, PGSIZE, PTE_U | PTE_W)，再 page_insert(p1, PGSIZE, 0)；
- 预期：page_ref(p1) == 2、page_ref(p2) == 0，新 PTE 不含 PTE_U / PTE_W；
- 失败现象：p2 引用未归零（泄漏）、或旧权限位残留（用 |= 拼接）。

测试 3（同页重复映射）：
- page_insert(p3, 2*PGSIZE, 0)，记录 nr_free_pages()，再 page_insert(p3, 2*PGSIZE, PTE_W)；
- 预期：page_ref(p3) == 1，空闲页数不变；
- 失败现象：引用变成 2（多加工次）、或页面被释放（先删旧项再加引用导致 use-after-free）。

测试 4（幂等删除）：
- 依次 page_remove(0)、page_remove(PGSIZE)、page_remove(2*PGSIZE)，再重复一次 page_remove(2*PGSIZE)；
- 预期：三个页引用都归零，重复删除不 panic、不重复释放；
- 失败现象：第二次删除触发 pte2page 对 V=0 的 panic，或重复 free 导致空闲链表损坏。

覆盖说明：
- 没有覆盖跨 2MiB 大页边界、Sv48、多 hart 并发映射、get_pte 分配失败（需要构造内存耗尽）。
````

### 6.3 代码审查 / 修 bug 提示词

````markdown
[PROMPT]
以代码审查者的身份，逐条检查 page_remove_pte()、page_insert()、page_remove() 是否满足实验约束。
不要重写代码；按「问题、位置、原因、修法、验证」逐条输出。重点检查以下八项：
1. page_insert 是否先 page_ref_inc(page)，再处理旧映射；
2. 同页重映射是否只做一次 page_ref_dec 抵消，没有走 free_page；
3. 异页时是否调用 page_remove_pte 完成「旧页 dec → 归零 free → 清项 → 刷 TLB」；
4. get_pte(pgdir, la, 1) 返回 NULL 时是否直接返回 -E_NO_MEM，且没有改动引用计数或 PTE；
5. 是否用整项覆盖 *ptep = pte_create(...)，而不是 |= 拼接旧权限；
6. page_remove 是否用 get_pte(..., 0)，且对 NULL / 无效项安静返回；
7. 每次 PTE 变更后是否调用 tlb_invalidate(pgdir, la)；
8. 是否误改了 get_pte()、check_pgdir()、check_boot_pgdir() 或头文件。

[RELY]
- 实验要求：PTE 内容、物理页引用计数、TLB 三者必须同步；
- pte2page(pte) 在 PTE_V=0 时会 panic；page_ref_dec 返回递减后的值；
- pte_create(ppn, perm) 自动带 PTE_V；
- test_lab4.c 的三个关键断言：换页旧页归零、同页重复插入不增引用、page_remove 连调两次幂等；
- 编译选项 -Wall -Werror。

[GUARANTEE]
- 输出问题清单，每条包含：文件/行、现象、原因、建议修改、验证方法；
- 只给修改建议，不直接改无关代码；
- 若代码没有问题，明确写「八项均通过」，不要为了凑问题硬编。

[SPECIFICATION]
逐项判定：
1. 先删旧项再加引用 → 同页重映射时旧项 dec 归零、页面被 free，新 PTE 指向已释放页；
2. 同页分支误走 page_remove_pte → 引用计数被多减一次，页面提前释放；
3. 异页分支忘记撤销旧项 → 旧页引用泄漏，空闲页数持续下降；
4. 失败路径改了引用计数 → 调用者认为映射失败，但页面引用已增加，造成泄漏；
5. 用 |= 拼接 → 旧 PTE_U / PTE_W 残留，权限被悄悄放大；
6. page_remove 用 create=1 → 删除未映射地址时反而分配两级页表页；
7. 不刷 TLB → 页表已更新但 CPU 仍命中旧翻译，出现「查表对、访问错」；
8. 改了框架代码 → 违反分工，且可能破坏 check_pgdir 的既有断言。
````

### 6.4 模块一这三份提示词分别解决什么问题

| 提示词 | 解决的问题 | 对应最终代码 / 测试 |
|---|---|---|
| 主实现 | 让 AI 一次写对「建立 / 更新 / 撤销」三条路径，尤其是引用计数顺序与失败回滚 | `kern/mm/pmm.c` 的 page_remove_pte / page_insert / page_remove |
| 测试生成 | 用引用计数与空闲页数证明换页、同页重映射、幂等删除都正确 | 5.1 页表相关测试、`[Test PMM] PASS` |
| 代码审查 | 抓「先删后加」「\|= 拼权限」「create=1 删除」这三类典型错误 | 4.1 的迭代记录 |

---

## 7. 功能模块二：进程创建及初始化（2510538-张铭瑞）

### 7.1 主实现提示词

````markdown
[PROMPT]
**任务**：修改 `kern/process/proc.c`，实现功能模块二「进程创建及初始化」：让内核能创建并登记多个内核线程。只改 `//LAB4 function2` 标记范围内的六个函数，按依赖顺序实现：
1. `alloc_proc()`——分配并初始化 PCB；
2. `copy_mm()`——当前阶段只创建内核线程，不复制用户地址空间；
3. `copy_thread()`——在新线程自己的内核栈上构造首次运行现场；
4. `do_fork()`——组合 PCB、内核栈、PID、链表与状态，失败时完整回滚；
5. `kernel_thread()`——把「函数 + 参数」包装成线程首次运行的 trapframe；
6. `proc_init()`——创建 idleproc 和第一个真正的内核线程 initproc。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留文件中的现有代码（`forkret` / `forkrets`、`setup_kstack` / `put_kstack`、`get_pid` / `hash_proc` / `find_proc`、`wakeup_proc`、`do_exit`、`init_main` 等），不要改动 `//LAB4 function3` 标记之后的调度代码，不要改 `kern/mm/pmm.c`、`kern/schedule/sched.c`、`kern/process/switch.S`、`Makefile` 和 `tools/`。允许新增 static 辅助函数。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 的信息，以工程真实代码为准；失败路径必须释放已申请资源；不得伪造 PID 或绕过 `alloc_proc()`。

[RELY]
// 数据结构（kern/process/proc.h）
enum proc_state { PROC_UNINIT = 0, PROC_SLEEPING, PROC_RUNNABLE, PROC_ZOMBIE };  // :10-15
struct context { uintptr_t ra, sp, s0, s1, ..., s11; };            // :18-33，共 14 个字段
struct proc_struct {                                               // :41-58
    enum proc_state state;        int pid;          int runs;
    uintptr_t kstack;             volatile bool need_resched;
    struct proc_struct *parent;   struct mm_struct *mm;
    struct context context;       struct trapframe *tf;
    uintptr_t pgdir;              uint32_t flags;
    char name[PROC_NAME_LEN + 1];
    list_entry_t list_link;       list_entry_t hash_link;
};
#define PROC_NAME_LEN 15                                           // :36
#define MAX_PROCESS   4096                                         // :37
#define MAX_PID       (MAX_PROCESS * 2)                            // :38
#define le2proc(le, member) to_struct((le), struct proc_struct, member)  // :60-61
extern struct proc_struct *idleproc, *initproc, *current;          // :63

// 内核栈（kern/mm/memlayout.h:41-42）
#define KSTACKPAGE 2
#define KSTACKSIZE (KSTACKPAGE * PGSIZE)

// 已有函数（kern/process/proc.c，不要修改）
static int  get_pid(void);                                         // :103
static void forkret(void) { forkrets(current->tf); }               // :149
static void hash_proc(struct proc_struct *proc);                   // :156
struct proc_struct *find_proc(int pid);                            // :163
static int  setup_kstack(struct proc_struct *proc);                // :183，alloc_pages(KSTACKPAGE)
static void put_kstack(struct proc_struct *proc);                  // :196，free_pages(..., KSTACKPAGE)
void wakeup_proc(struct proc_struct *proc);                        // sched.c:8，assert 非 ZOMBIE/RUNNABLE 后置 RUNNABLE

// 全局量 / 接口
list_entry_t proc_list;                          // proc.c:62，进程链表头
static list_entry_t hash_list[HASH_LIST_SIZE];   // proc.c:69，仅 proc.c 可见
static int nr_process;                           // proc.c:78，仅 proc.c 可见
uintptr_t bootstack;                             // 启动栈，idleproc 直接复用
uintptr_t boot_pgdir_pa;                         // 内核页目录物理地址
void *kmalloc(size_t n);  void kfree(void *p);
void list_init(list_entry_t *el);  void list_add(list_entry_t *head, list_entry_t *el);
uintptr_t read_csr(unsigned long csr);  void write_csr(unsigned long csr, uintptr_t val);
char *set_proc_name(struct proc_struct *proc, const char *name);
void panic(const char *fmt, ...);  void assert(...);

// 首次运行链路与常量
// kernel_thread_entry 从 s0 取 fn、s1 取 arg；forkret → forkrets(current->tf)
//   → __trapret → sret → tf->epc（= kernel_thread_entry）
#define CLONE_VM      0x00000100        // kern/mm/pmm.h:11
#define SSTATUS_SIE   0x00000002        // libs/riscv.h:46
#define SSTATUS_SPIE  0x00000020        // libs/riscv.h:48
#define SSTATUS_SPP   0x00000100        // libs/riscv.h:49
#define E_NO_MEM      4                 // libs/error.h
#define E_NO_FREE_PROC 5                // libs/error.h

[GUARANTEE]
必须实现或修改的接口（同一功能模块，可加 static 辅助函数）：
```c
static struct proc_struct *alloc_proc(void);
static int  copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
int  do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int  kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
void proc_init(void);
```
不得修改 `//LAB4 function2` 范围之外的函数，不得改其他模块的文件、`Makefile` 与 `tools/`。

[SPECIFICATION]
## alloc_proc
**Pre-Condition**:
- `pmm_init()` / `kmalloc_init()` 已完成，`kmalloc` 可用。

**Post-Condition**:
- 成功返回一块已清零并完成初始化的 PCB；失败返回 NULL。
- 字段状态：`state = PROC_UNINIT`、`pid = -1`、`runs = 0`、`kstack = 0`、`need_resched = 0`、`parent = NULL`、`mm = NULL`、`tf = NULL`、`pgdir = boot_pgdir_pa`、`flags = 0`、`name` 全零、`list_link` / `hash_link` 自环。

  **Case 1（kmalloc 成功）**:
  - 先 `memset` 清零整块 PCB，再逐字段赋初值，最后 `list_init` 两个链表节点。

  **Case 2（kmalloc 失败）**:
  - 返回 NULL，不写任何全局状态。

**Requirements**:
- 必须先清零再赋初值，避免未初始化字段影响状态判断；
- 链表节点必须用 `list_init`（空节点的 prev/next 指向自己，不是 NULL）；
- `state` 必须是 `PROC_UNINIT`，保证 PCB 在完整初始化前不会被调度器选中。

## copy_mm
**Pre-Condition**:
- `current->mm == NULL`（lab4 只创建内核线程，共享内核地址空间）。

**Post-Condition**:
- `proc->mm = NULL`，返回 0。

**Requirements**:
- 不复制页表、不创建 VMA；用 `assert(current->mm == NULL)` 固化前提；`clone_flags` 可显式 `(void)` 掉以避免 `-Werror`。

## copy_thread
**Pre-Condition**:
- `proc->kstack` 已由 `setup_kstack()` 分配；
- `tf` 指向调用者构造的临时 trapframe（`kernel_thread()` 的栈上变量）。

**Post-Condition**:
- `proc->tf` 位于 `proc->kstack + KSTACKSIZE - sizeof(struct trapframe)`；
- `*(proc->tf) = *tf`；
- `proc->tf->gpr.a0 = 0`（子线程从 do_fork 返回 0）；
- `proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp`；
- `proc->context.ra = (uintptr_t)forkret`；
- `proc->context.sp = (uintptr_t)(proc->tf)`。

  **Case 1（esp == 0，内核线程）**:
  - 没有用户栈，栈指针指向自己的 trapframe 顶部。

  **Case 2（esp != 0，将来的用户进程）**:
  - 使用调用者传入的用户栈指针。

**Requirements**:
- trapframe 必须放在内核栈顶且不越界（`KSTACKSIZE` 是 2 页）；
- `context.sp` 必须指向 trapframe，`forkrets` 才能通过 `move sp, a0` 正确切换栈。

## do_fork
**Pre-Condition**:
- `current` 有效；`nr_process` 反映当前进程数。

**Post-Condition**:
- 成功返回子线程 pid（> 0），子线程已加入 `hash_list` 与 `proc_list`，状态为 `PROC_RUNNABLE`；
- 失败返回负错误码，且不泄漏 PCB、内核栈或链表节点。

  **Case 1（nr_process >= MAX_PROCESS）**:
  - 返回 `-E_NO_FREE_PROC`，不做任何分配。

  **Case 2（alloc_proc 失败）**:
  - 返回 `-E_NO_MEM`。

  **Case 3（setup_kstack 失败）**:
  - `kfree(proc)` 后返回 `-E_NO_MEM`。

  **Case 4（copy_mm 失败）**:
  - `put_kstack(proc)` + `kfree(proc)` 后返回错误码。

  **Case 5（成功）**:
  - 依次：`parent = current` → `setup_kstack` → `copy_mm` → `copy_thread` → `pid = get_pid()` → `hash_proc` → `list_add(&proc_list, &proc->list_link)` → `nr_process++` → `wakeup_proc(proc)` → 返回 pid。

**Requirements**:
- 要么全成功，要么全回退，不允许「一部分成功、一部分失败」；
- `parent` 必须在 `setup_kstack` 之前设置（initproc 的 parent 必须是 idleproc）；
- pid 与链表登记必须在 `copy_thread` 之后、`wakeup_proc` 之前完成；
- 失败标签按「后申请的先释放」顺序排列。

## kernel_thread
**Pre-Condition**:
- `fn` 非空；当前处于 S 态，可读 `sstatus`。

**Post-Condition**:
- 返回 `do_fork()` 的结果；
- 子线程首次运行的 `tf->epc = kernel_thread_entry`，`tf->gpr.s0 = fn`，`tf->gpr.s1 = arg`；
- `tf->status` 置 `SSTATUS_SPP | SSTATUS_SPIE` 并清 `SSTATUS_SIE`。

**Requirements**:
- 临时 trapframe 在栈上，必须先 `memset` 清零；
- `clone_flags` 必须或上 `CLONE_VM`（内核线程共享地址空间）；
- 只构造现场并转调 `do_fork`，不得直接调用 `fn`。

## proc_init
**Pre-Condition**:
- `pmm_init()` / `kmalloc_init()` 已完成；当前没有父进程，只有 `kern_init` 的执行现场。

**Post-Condition**:
- `proc_list` 与 `hash_list` 全部初始化；
- `idleproc`：pid = 0、state = `PROC_RUNNABLE`、kstack = `bootstack`、need_resched = 1、name = "idle"、parent = NULL、mm = NULL、tf = NULL、runs = 0、flags = 0、context.ra = 0；
- `current == idleproc`；
- `initproc`：pid = 1、parent = idleproc、state = `PROC_RUNNABLE`、name = "init"、context.ra / sp 非 0；
- 结尾断言 `idleproc->pid == 0` 且 `initproc->pid == 1`。

  **Case 1（alloc_proc 失败）**:
  - `panic("cannot alloc idleproc.\n")`。

  **Case 2（kernel_thread 返回 <= 0）**:
  - `panic("create init_main failed.\n")`。

**Requirements**:
- idleproc 不通过 fork 创建，直接复用启动现场（`bootstack`）；
- initproc 必须由 `kernel_thread(init_main, "Hello world!!", 0)` 创建，并用 `find_proc(pid)` 取回；
- `nr_process` 的计数必须与链表登记保持一致。

验收：
1. `make` 无 undefined reference（模块三未合入时 `cpu_idle` / `schedule` / `proc_run` / `switch_to` 未定义属正常）；
2. 注入测试后打印 `[Test Process Create] PASS`；
3. `idleproc` 的 pid / state / kstack / parent / mm / tf / runs / flags / context.ra 全部符合上面的 Post-Condition；
4. `initproc` 的 pid == 1、parent == idleproc、context.ra / sp 非 0。
````

### 这份提示词的关键点（模块二）

| 段落 | 为什么这么写 |
|---|---|
| `[PROMPT]` | 把六个函数按依赖顺序编号，并划清「不碰模块三、不碰 pmm.c、不碰 tools」的边界，避免 AI 为了消除未定义符号去改别人的文件 |
| `[RELY]` | 给出 `struct proc_struct` 完整字段、`KSTACKSIZE`、`forkret → forkrets → sret → kernel_thread_entry` 链路，这些是 AI 靠猜必错、写进提示词就能一次到位的事实 |
| `[GUARANTEE]` | 六个签名 + 明确允许 static 辅助，防止 AI 擅自改公开接口或新增全局函数 |
| `[SPECIFICATION]` | 每个函数都写了 Post-Condition 和失败分支，尤其 `do_fork` 的四条回滚路径，是「要么全成功要么全回退」的验收依据 |

### 7.2 测试生成提示词

````markdown
[PROMPT]
针对已经实现的 alloc_proc() / copy_mm() / copy_thread() / do_fork() / kernel_thread() / proc_init()，设计并生成验证方案，用来证明：
1. proc_init() 之后 idleproc 与 initproc 的字段完全符合规格；
2. do_fork() 在进程数达到 MAX_PROCESS 时返回 -E_NO_FREE_PROC，且不产生任何分配；
3. kernel_thread() 创建的线程已获得唯一 pid、进入 PROC_RUNNABLE、并出现在 hash 表与 proc_list 中；
4. 失败路径不会泄漏 PCB 或内核栈。
要求给出测试代码、插入位置、预期输出与判定方法；测试代码不能混进最终交付。

[RELY]
- 已有测试框架：tools/test_lab4.c 的 test_lab4_process_create()，tools/inject.py 会把它注入 proc_init 末尾；
- 可用接口：alloc_proc（static，测试可放在 proc.c 内）、do_fork、kernel_thread、find_proc、page_ref / nr_free_pages；
- 断言用 assert()，输出用 cprintf；
- 判定 sentinel："[Test Process Create] PASS"；
- 模块三未合入时线程无法真正运行，测试只能验证「创建结果」，不能验证「执行结果」。

[GUARANTEE]
- 不修改 tools/ 下的框架测试；
- 临时验证代码只放临时副本，验证后删除，不混进最终提交；
- 每个用例给出预期现象和失败现象；
- 明确写出没有覆盖的场景（真实用户地址空间、进程退出与回收、调度执行）。

[SPECIFICATION]
测试 1（idleproc / initproc 字段）：
- 在 proc_init 末尾断言：
  current == idleproc；idleproc->pid == 0；state == PROC_RUNNABLE；
  kstack == (uintptr_t)bootstack；parent == NULL；mm == NULL；tf == NULL；
  runs == 0；flags == 0；context.ra == 0；
  initproc != NULL；initproc->pid == 1；state == PROC_RUNNABLE；
  parent == idleproc；mm == NULL；context.ra != 0；context.sp != 0。
- 预期：全部通过并打印 [Test Process Create] PASS；
- 失败现象：idleproc 的 kstack 不是 bootstack（误走 fork）、initproc 的 parent 不是 idleproc、或 context 未设置。

测试 2（进程数上限）：
- 临时把 nr_process 置为 MAX_PROCESS，调用 kernel_thread(...)；
- 预期：返回 -E_NO_FREE_PROC，nr_free_pages() 不变；
- 失败现象：返回 0 或正数、或空闲页减少（先分配后检查）。

测试 3（创建结果登记）：
- 调用 kernel_thread(fn, arg, 0)，拿到 pid；
- 预期：pid > 0，find_proc(pid) 非空且 pid 一致，state == PROC_RUNNABLE，list_link 已挂入 proc_list；
- 失败现象：find_proc 返回 NULL（忘 hash_proc）、state 仍为 PROC_UNINIT（忘 wakeup_proc）。

覆盖说明：
- 没有覆盖真实用户地址空间复制（copy_mm 当前是空壳）、进程退出与僵尸回收、调度器真正切换到新线程（属于模块三）。
````

### 7.3 代码审查 / 修 bug 提示词

````markdown
[PROMPT]
以代码审查者的身份，逐条检查 alloc_proc()、copy_mm()、copy_thread()、do_fork()、kernel_thread()、proc_init() 是否满足实验约束。
不要重写代码；按「问题、位置、原因、修法、验证」逐条输出。重点检查以下八项：
1. alloc_proc 是否先 memset 清零，再设置全部字段，并对两个链表节点调用 list_init；
2. alloc_proc 是否把 state 置为 PROC_UNINIT、pid 置为 -1、pgdir 置为 boot_pgdir_pa；
3. do_fork 的失败路径是否完整回滚（setup_kstack 失败 → kfree；copy_mm 失败 → put_kstack + kfree）；
4. do_fork 是否先检查 nr_process >= MAX_PROCESS，再分配；
5. do_fork 的顺序是否是 copy_thread → get_pid → hash_proc → list_add → nr_process++ → wakeup_proc；
6. copy_thread 是否把 tf 放在 kstack + KSTACKSIZE - sizeof(tf)，a0 置 0，context.ra 置 forkret；
7. kernel_thread 是否设置 s0 = fn、s1 = arg、epc = kernel_thread_entry，status 置 SPP/SPIE 并清 SIE，且 clone_flags 带上 CLONE_VM；
8. proc_init 是否让 idleproc 复用 bootstack、current = idleproc、initproc = find_proc(pid)，并保留两条断言。

[RELY]
- 实验要求：PCB、内核栈、trapframe、context 四者必须一致；
- wakeup_proc 会 assert 目标状态不是 ZOMBIE / RUNNABLE，所以调用前 state 必须是 PROC_UNINIT；
- forkret 只做 forkrets(current->tf)，所以 current 必须先指向新线程；
- KSTACKPAGE = 2、KSTACKSIZE = 2 * PGSIZE；
- 编译选项 -Wall -Werror。

[GUARANTEE]
- 输出问题清单，每条包含：文件/行、现象、原因、建议修改、验证方法；
- 只给修改建议，不直接改无关代码；
- 若代码没有问题，明确写「八项均通过」，不要为了凑问题硬编。

[SPECIFICATION]
逐项判定：
1. 没清零 / 漏字段 → 残留垃圾值让 state、pgdir、need_resched 判断出错；漏 list_init → list_add 解引用野指针；
2. state 不是 UNINIT → PCB 可能在初始化完成前被调度；pgdir 不是 boot_pgdir_pa → 新线程切过去后页表错误；
3. 失败不回滚 → PCB / 内核栈泄漏，长时间 fork 会耗尽内存；
4. 先分配后检查上限 → 达到上限时仍浪费一次 PCB 与内核栈；
5. 顺序错误 → pid 未分配就 hash（哈希错槽）、或未入链表就 wakeup（调度器找不到）；
6. tf 位置算错 → 覆盖内核栈上的其他数据或越界；a0 不为 0 → 子线程误以为自己是被 fork 的父进程；
7. s0/s1/epc 设置错 → kernel_thread_entry 取不到函数或参数；忘 CLONE_VM → 语义上多了一层本不存在的地址空间；
8. idleproc 走了 fork → kstack 不是 bootstack，启动现场丢失；initproc 不用 find_proc 取回 → 全局指针为空。
````

### 7.4 模块二这三份提示词分别解决什么问题

| 提示词 | 解决的问题 | 对应最终代码 / 测试 |
|---|---|---|
| 主实现 | 让 AI 按依赖顺序写对六个函数，尤其是 `do_fork` 的回滚路径与 `copy_thread` 的首次现场 | `kern/process/proc.c` 的 function2 段 |
| 测试生成 | 用字段断言证明 idleproc / initproc 的规格，用上限与泄漏检查证明失败路径干净 | 5.2 进程创建测试、`[Test Process Create] PASS` |
| 代码审查 | 抓「漏初始化」「失败不回滚」「先分配后检查」「忘 wakeup_proc」这四类典型错误 | 4.2 的迭代记录 |
