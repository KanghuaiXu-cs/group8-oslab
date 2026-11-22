# Lab4 进程调度提示词套件

> 使用顺序：先 1（实现）→ 再 2（测试）→ 再 3（审查）；如果审查发现问题，用 3 的输出回到 1 的 `[SPECIFICATION]` 补约束。
> 本套件只覆盖**功能模块三（进程调度）**；模块一（页表操作）、模块二（进程创建）的提示词由对应负责人补充。

---

## 1. 主实现提示词

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

## 2. 测试生成提示词

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

## 3. 代码审查 / 修 bug 提示词

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

## 4. 这三份提示词分别解决什么问题

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
