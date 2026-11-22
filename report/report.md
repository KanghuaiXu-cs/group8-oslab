# 实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 4：进程管理（虚拟内存页操作、进程创建、进程调度） |
| **小组成员** | 2510789-徐康怀、2514022-邓翔予、2510538-张铭瑞 |
| **完成日期** | 2026-11-20 |

### 小组分工

练习/模块分工：

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2514022-邓翔予 | 功能模块一：虚拟内存页操作（`page_insert`、`page_remove`） |
| 2510538-张铭瑞 | 功能模块二：进程创建及初始化（`alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`）+ Challenge |
| 2510789-徐康怀 | 功能模块三：进程调度（`cpu_idle`、`schedule`、`proc_run`、`switch_to`） |

实验报告分工：

| 成员 | 负责章节 |
|------|----------|
| 2514022-邓翔予 | 4.1 功能模块一；5.1 页表相关测试 |
| 2510538-张铭瑞 | 三、整体逻辑；4.2 功能模块二；4.4 Challenge；5.2 进程创建测试；六、总结 |
| 2510789-徐康怀 | 4.3 功能模块三；5.3 调度测试；二、实验环境 |

> 说明：4.1、4.2、4.4 的“最终提示词 / 实现迭代过程”由对应负责人补充，本文先记录各模块的接口、集成行为与测试结果；模块三（4.3）由徐康怀完整撰写。

---

## 一、实验目的

本实验的主要目的是：

1. 理解**多级页表的映射维护**：`page_insert` / `page_remove` 如何建立与撤销“线性地址 → 物理页”的映射，以及页表项内容、物理页引用计数、TLB 三者为什么必须保持一致。
2. 理解**进程（线程）控制块与地址空间**：`struct proc_struct` 里每个字段的含义，内核栈、trapframe、`struct context` 三者在“线程被创建”和“线程第一次运行”时分别承担什么角色。
3. 理解**调度机制与调度策略的分离**：`schedule()` 负责“选谁”，`proc_run()` 负责“切过去”，`switch_to()` 只负责“保存/恢复寄存器现场”。
4. 理解**上下文切换的调用约定依据**：为什么 `switch_to` 只需要保存 `ra`、`sp`、`s0`~`s11`，而不是像 lab3 的 `RESTORE_ALL` 那样保存全部 32 个通用寄存器。
5. 理解**新线程的首次运行链路**：`forkret → forkrets → __trapret → sret → kernel_thread_entry` 是如何把“一个函数”变成“一个线程”的。
6. 练习“先写 Spec、再写 Prompt、让 AI 生成、自己验证”的协作方式。

---

## 二、实验环境

| 组件 | 版本/说明 |
|---|---|
| 宿主系统 | Windows 11 + WSL2 |
| Linux 发行版 | Ubuntu（WSL2） |
| 交叉编译器 | `riscv64-unknown-elf-gcc 13.2.0`；本地验证用 xpack `riscv-none-elf-gcc 13.3.0`（需显式 `-march=rv64gc_zicsr -mabi=lp64d`） |
| 模拟器 | 演示/验收：QEMU 4.1.1；本地开发验证：QEMU 10.2 |
| 固件 | OpenSBI（`-bios default`；4.1.1 对应 v1.3，10.2 对应 v1.8） |
| 构建方式 | `make` / `make qemu` / `make grade` |

AI 工具：

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2514022-邓翔予 | 待组员补充 | 待组员补充 | — |
| 2510538-张铭瑞 | 待组员补充 | 待组员补充 | — |
| 2510789-徐康怀 | DeepSeek Harness（VS Code 集成） | deepseek-flash | 用于调度代码生成、审查与本地验证脚本 |

> **QEMU 版本差异（重要）**：课程要求 QEMU 4.1.1，本地只有 10.2，验证时遇到两处版本差异：
> 1. QEMU ≥ 9 下 `-device loader` 会让 OpenSBI 的 `next_addr` 变成 0，内核不启动；本地改用 `-kernel bin/ucore.img`。`Makefile` / `tools/grade.sh` 保持原样，未做任何修改。
> 2. QEMU 10 会检查“非叶子页表项的保留位”，而框架的 `get_pte()` 给中间级页表项写了 `PTE_U`，导致新映射一访问就 page fault（现象是 `check_pgdir()` 之后内核“重启”）。这是**框架代码 + QEMU 版本**的差异，4.1.1 上不存在；本地验证只在临时副本里打补丁，交付代码保持原样。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

Lab2 让内核能管物理内存，Lab3 让内核能被打断并回到原处。但这两章的内核仍然是“一个执行流”：它能被人打断，却**没有“多个执行流”的概念**。

Lab4 要补的是：

> **把“一块内存 + 一段代码”包装成可以被保存、被恢复、被排队、被切换的执行实体（线程），并让 CPU 在多个线程之间流转。**

整章的三条主线正好对应三个模块：

| 主线 | 模块 | 解决的问题 |
|---|---|---|
| 虚拟内存页操作 | 一 | 一个线性地址最终对应哪一页物理内存，怎么建立/撤销 |
| 进程创建与初始化 | 二 | 线程对象从哪来：PCB、内核栈、trapframe、首次运行现场 |
| 进程调度 | 三 | 线程对象怎么真正跑起来：选谁、切页表、切寄存器现场 |

三者的依赖顺序是**由下往上**的：页表操作是地址空间的地基；进程创建需要页表和内核栈；“能不能切过去”又要求先“有可切的对象”。所以模块三只能放在最后——它不是“创建资源”，而是“让已有资源动起来”。

### 3.2 功能的逐步实现

整个 lab4 的启动链是：

```
kern_init
  → pmm_init      （物理内存 + 页表 + kmalloc）
  → pic_init / idt_init
  → vmm_init      （mm / vma 结构与检查）
  → proc_init     （idleproc + initproc）
  → clock_init / intr_enable
  → cpu_idle      （模块三入口）
      → schedule        （选谁）
          → proc_run    （切 current、切页表）
              → switch_to （切寄存器现场）
```

四个“为什么按这个顺序”：

1. **先做页表操作（模块一）**：`page_insert`/`page_remove` 是“线性地址 ↔ 物理页”的唯一入口。没有它，`check_boot_pgdir()`、`kmalloc` 都无法通过；而后面线程的内核栈、trapframe 全都要落在这套映射上。
2. **再做进程创建（模块二）**：`alloc_proc` 给出干净的 PCB，`proc_init` 复用当前启动现场造出 `idleproc`、再用 `kernel_thread` 造出 `initproc`。`copy_thread` 把 trapframe 放到新线程自己的内核栈顶，并把 `context.ra` 设成 `forkret`——这一步是**为模块三准备“路标”**。
3. **然后做调度（模块三）**：有了 `idleproc` 和 `initproc`，`cpu_idle → schedule → proc_run → switch_to` 才能把它们真正放到 CPU 上跑。模块三不创建任何资源，只负责“选”和“切”。
4. **最后用测试验收**：`[Test PMM]` 验证映射；`[Test Process Create]` 验证 `idleproc/initproc` 的字段；`[Test Schedule]` 额外创建两个线程并反复切换，验证“切换后还能切回来、两个线程都真正执行过”。

一句话：**先把地址空间修好，再把线程对象造出来，最后让 CPU 在它们之间流转。**

---

## 四、实验内容与实现

### 4.1 功能模块一：虚拟内存页操作

**负责人：** 2514022-邓翔予

#### 模块功能描述

**需要实现/修改的函数：**

```c
int  page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

**功能说明：**

在现有多级页表框架上完成最基本的映射维护：把物理页挂到指定线性地址上，以及把已有映射安全地撤销掉。两者共同决定“一个线性地址最终对应哪一页物理内存”，并负责让**页表项内容、物理页引用计数、TLB 状态**三者保持一致。对外表现为“建立映射”和“撤销映射”两个动作，是后续线程地址空间逻辑的基础。

#### 最终提示词

> 由负责人补充。

#### 实现迭代过程

> 由负责人补充。

---

### 4.2 功能模块二：进程创建及初始化

**负责人：** 2510538-张铭瑞

#### 模块功能描述

**需要实现/修改的函数：**

```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int  kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int  do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int  copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

**功能说明：**

把系统从“能跑内核代码”推进到“能创建并登记多个内核线程”：`alloc_proc` 拿到干净的 PCB；`proc_init` 把当前执行现场包装成 `idleproc` 并创建第一个真正的内核线程 `initproc`；`kernel_thread` 把函数入口和参数整理成线程首次运行所需的临时 trapframe；`do_fork` 把 PCB、内核栈、PID、链表、状态位组合成可运行实体；`copy_thread` 把 trapframe 和 `context` 安放到新线程自己的内核栈上；`copy_mm` 在当前阶段是地址空间处理的占位接口。

其中与模块三直接相关的“路标”由 `copy_thread` 设置：

```c
proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
*(proc->tf) = *tf;                    // 复制 kernel_thread() 构造的临时现场
proc->tf->gpr.a0 = 0;                 // 子线程从“fork 返回 0”
proc->tf->gpr.sp = (uintptr_t)proc->tf;
proc->context.ra = (uintptr_t)forkret; // 第一次被调度时从这里开始
proc->context.sp = (uintptr_t)(proc->tf);
```

#### 最终提示词

> 由负责人补充。

#### 实现迭代过程

> 由负责人补充。

---

### 4.3 功能模块三：进程调度

**负责人：** 2510789-徐康怀

#### 模块功能描述

**需要实现/修改的函数：**

```c
// kern/process/proc.c
void cpu_idle(void);
void proc_run(struct proc_struct *proc);

// kern/schedule/sched.c
void schedule(void);

// kern/process/switch.S
void switch_to(struct context *from, struct context *to);
```

**功能说明：**

把已经创建出来的内核线程真正交给 CPU 执行。四个函数职责严格分层：

| 函数 | 职责 | 不做什么 |
|---|---|---|
| `cpu_idle` | 空闲线程主体，检测到 `need_resched` 就进调度器 | 不决定选谁 |
| `schedule` | 从 `proc_list` 里选出下一个 `PROC_RUNNABLE`，找不到回退 `idleproc` | 不碰硬件寄存器现场 |
| `proc_run` | 更新 `current`、切 `satp` 并刷 TLB、调用 `switch_to` | 不做策略判断 |
| `switch_to` | 只保存/恢复 `ra`、`sp`、`s0`~`s11` | 不做任何调度决策 |

这四层的关系其实就是**机制与策略分离**：`schedule` 是策略，`proc_run`/`switch_to` 是机制。

#### 最终提示词

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

**这份提示词的关键点：**

| 段落 | 为什么这么写 |
|---|---|
| `[PROMPT]` | 精确到“文件 + 标记 + 函数”，避免 AI 顺手改掉模块二/模块一的代码 |
| `[RELY]` | 把 `struct context` 的字段顺序、`copy_thread` 设的路标、`pgdir` 是物理地址、`lsatp()` 是 Sv32 陷阱这些**真实事实**喂给 AI，否则它会按“普通过程调用”瞎猜 |
| `[GUARANTEE]` | 把“只存 14 个寄存器”“必须关中断”“必须 flush_tlb”“必须回退 idleproc”写成硬约束 |
| `[SPECIFICATION]` | 每个函数写成可执行步骤 + 边界条件，尤其 `schedule` 的起点分情况、`proc_run` 的 satp 拼法 |

#### 实现迭代过程

本模块的实现经历了 3 次迭代。

---

##### 第一次迭代

**遇到的问题：**

1. **`proc_run` 想偷懒用 `lsatp()`**：`libs/riscv.h` 里确实有个 `lsatp(pgdir)`，看起来就是“设置页表”。但它写的是 `SATP32_MODE | (pgdir >> RISCV_PGSHIFT)`，其中 `SATP32_MODE = 0x80000000`——那是 Sv32 的 MODE 位，Sv39 的 MODE 是值为 8 的 4 位字段、要放在 bit 63..60。直接用会导致 satp 里 MODE 字段完全错误。
2. **`schedule` 的轮转起点**：第一版用 `&proc_list` 作为起点，逻辑上“从头找”看着没问题，但这样就**没有从 current 的下一个开始轮转**，`idleproc` 会一直被优先选中，调度不公平；若改成 `&current->list_link`，当 `current == idleproc` 时这个节点根本不在链表里，`list_next` 会走进野指针。
3. **`switch_to` 差点照搬 lab3 的结论**：lab3 的 `RESTORE_ALL` 有一条铁律“sp 必须最后恢复”，第一版注释里也这么写了。但那是**因为 `RESTORE_ALL` 以 sp 为基址**；`switch_to` 的基址是 `a0`/`a1`，sp 只是被存取的数据之一，不存在这个约束。

**问题解决策略：**

- 针对 1：在 `[RELY]` 里明确写“`lsatp()` 是 Sv32 遗留写法，禁止使用；照 `enable_paging()` 自己拼 satp”，并在生成后逐字核对 `SATP_MODE_SV39 << 60`。
- 针对 2：在 `[SPECIFICATION]` 里把起点写成 `last = (current == idleproc) ? &proc_list : &(current->list_link)`，并说明“idleproc 不在 proc_list 里”。
- 针对 3：在 `[RELY]` 里补上“`switch_to` 的存取基址是 a1，不是 sp”，并在注释里写明“这里没有 sp 最后恢复的约束”。

---

##### 第二次迭代

**遇到的问题：**

1. **`need_resched` 没有清零**：第一版 `schedule` 忘了 `current->need_resched = 0`，结果线程一被切过去就发现“有人请求调度”，立刻又切走，形成空转（现象是 `runs` 疯涨、几乎没有实际执行）。
2. **临界区只包住了 `proc_run`**：第一版把 `local_intr_save` 放在 `proc_run` 里，而“选谁”这一步在临界区之外。理论上选完线程、切换之前来一次时钟中断，中断里的 `schedule` 可能改掉 `current`，造成 `current` 与实际现场不一致。

**问题解决策略：**

- 针对 1：在 `[SPECIFICATION]` 的 `schedule` 步骤里显式加上“先清 `need_resched`”，并解释原因（避免刚切回来又被切出去）。
- 针对 2：把临界区扩大到整个 `schedule`（选 + 切），`proc_run` 内部再保留自己的一层，形成“外层保证选择与切换的原子性、内层保证切换本身的原子性”。

---

##### 第三次迭代

**遇到的问题：**

本地 QEMU 10 上，内核跑到 `check_pgdir() succeeded!` 之后就像“重启”了一样，输出 `DTB Init / HartID: 256 ...`，最后 `panic at pmm.c:105`。一开始怀疑是自己的 `proc_run` 切 satp 切坏了。

**排查过程（这部分记录在 5.3 与本段）：**

1. `qemu -d int` 抓 trap：真正出错的是 `store_page_fault, tval=0x100`，位置在 `check_boot_pgdir()` 的 `strcpy((void *)0x100, str)`——发生在**调度之前**，说明与模块三无关；
2. 当时 `stvec` 还是 0（`idt_init()` 在 `pmm_init()` 之后），于是 PC 跳到 0；VA 0 与 0x100 同页且已被映射成数据页，CPU 把数据当指令执行，最终乱跳到 `kern_entry`，表现为“重启”；
3. 用 gdb 在 `strcpy` 前把三级页表逐级读出来，叶子 PTE 是 `0x...c7`（V|R|W|A|D 都在），**映射本身有效**；
4. 怀疑过 A/D 位（QEMU 10 需要 `menvcfg.ADUE=1` 才由硬件置位），补上 `PTE_A|PTE_D` 后仍然缺页，排除；
5. 最终定位：框架 `get_pte()` 给**非叶子**页表项写了 `PTE_U`，而 RISC-V 规范要求非叶子 PTE 的 D/A/U 保留位清零；QEMU 10 检查这一点，QEMU 4.1.1 不检查。

**最终结果：**

经过 3 次迭代，该模块最终：

- ✅ 编译通过（`-Werror`，无警告）
- ✅ `[Test PMM]`、`[Test Process Create]`、`[Test Schedule]` 三项全部 PASS
- ✅ `initproc` 成功运行并打印 `Hello world!!`
- ✅ 连跑 3 次结果稳定

**关键改进点总结：**

1. 在 `[RELY]` 中补充了 `copy_thread` 设置的“路标”（`context.ra/sp`、`tf->epc`），让 AI 明白“为什么 ret 会跑到 forkret”；
2. 在 `[SPECIFICATION]` 中把 `schedule` 的起点分情况和 `need_resched` 清零写成显式步骤；
3. 在 `[RELY]` 中把 `lsatp()` 标为禁用项，避免了 Sv32/Sv39 混用；
4. 明白了“本地 QEMU 跑不通”与“代码写错”是两件事：**先用 `-d int` 定位 trap，再判断是软件还是模拟器差异**，并且不为了本地跑通去改 `Makefile`/`tools/`。

---

### 4.4 Challenge

**负责人：** 2510538-张铭瑞

> 由负责人补充。

---

## 五、测试与验证

### 5.1 页表相关测试

> 由负责人补充（预期输出 `check_alloc_page() succeeded!`、`check_pgdir() succeeded!`、`check_boot_pgdir() succeeded!`、`[Test PMM] PASS`）。

### 5.2 进程创建测试

> 由负责人补充（预期输出 `[Test Process Create] PASS`，且 `idleproc->pid == 0`、`initproc->pid == 1`）。

### 5.3 调度测试（模块三）

**测试方式**：`tools/inject.py` 会把 `test_lab4_schedule()` 注入到 `init_main()` 的**开头**。它做三件事：

1. 先断言 `current == idleproc`、`idleproc->pid == 0`、`initproc->pid == 1`；
2. 用 `kernel_thread()` 再创建两个线程，每个线程循环 `need_resched = 1; schedule();`；
3. 自己在 `init_main` 里连续 8 次 `schedule()`，最后断言“两个线程都真的执行过”，打印 `[Test Schedule] PASS`。

**实测输出**（本地 QEMU 10.2 + xpack 工具链，注入官方测试）：

```
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
kmalloc_init() succeeded!
[Test PMM] PASS
check_vma_struct() succeeded!
check_vmm() succeeded.
[Test Process Create] PASS
++ setup timer interrupts
[Test Schedule] PASS            ← 模块三的验收点
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
kernel panic at kern/process/proc.c: process exit!!.
```

**结果判读：**

- `[Test Schedule] PASS` 说明：`cpu_idle → schedule → proc_run → switch_to → forkret → forkrets → __trapret → sret → kernel_thread_entry` 这整条链路是通的，而且**能来回切**（两个新线程都被执行过）；
- 紧随其后的 `process exit!!` 是**预期行为**：`init_main` 返回后 `kernel_thread_entry` 会 `jal do_exit`，lab4 的 `do_exit` 就是 `panic`；
- 连跑 3 次，三项 PASS 与 initproc 三行输出稳定出现（`3/3`、`1/1`），说明不是偶然。

**测试截图：**

> 提交前在此插入 **QEMU 4.1.1 上 `make grade`** 的实际截图（4.1.1 上无需任何临时补丁）。上面的文本输出是本地 QEMU 10.2 的等价结果，供比对。

---

## 六、实验总结与收获

### 对操作系统的理解

**1. 本实验的重要知识点，与 OS 原理的对照**

| 本实验的知识点 | OS 原理中的对应 | 关系与差异 |
|---|---|---|
| `switch_to` 保存 `ra/sp/s0~s11` | 上下文切换（context switch） | 原理讲“切换要保存/恢复 CPU 现场”；实验把“现场”具体化为 14 个寄存器，并能解释为什么只存这 14 个（调用约定） |
| `proc_run` 切 `current` + 切 `satp` | 进程切换（process switch） | 原理里“进程切换 = 换页表 + 换寄存器”；实验里两者被拆成两步，且内核线程共享 `boot_pgdir`，页表切换看起来“没变”但语义上仍要执行 |
| `schedule` 的轮转扫描 | 调度算法（RR） | 原理讲优先级、时间片、多级队列；实验只实现最简单的轮转，且是**协作式**（靠 `need_resched` 主动让出），不是抢占式 |
| `local_intr_save/restore` | 临界区 / 互斥 | 原理讲锁、信号量；单核内核里“关中断”是最轻量的互斥手段，代价是屏蔽所有中断 |
| `idleproc` | 空闲任务（Idle） | 原理里 idle 是最低优先级进程；实验里它直接复用启动现场，`kstack = bootstack`，没有走 `fork` |
| `forkret → forkrets → __trapret → sret` | 新进程的首次运行 / fork 的返回点 | 原理讲“子进程从 fork 返回 0”；实验用“构造 trapframe + 让 sret 跳进去”伪造这个返回点，`tf->gpr.a0 = 0` 就是“子进程看到返回 0” |
| `flush_tlb()` 与 `satp` | 页表基址寄存器与 TLB | 原理讲 TLB 是页表的缓存；实验必须在改 `satp` 后显式 `sfence.vma`，否则可能命中旧翻译 |

**2. OS 原理中很重要、但本实验没有对上的知识点**

1. **抢占式调度与时间片**：lab4 的调度是协作式的，时钟中断只推进 `ticks`，并没有在中断里强制调度。
2. **调度算法的评价与实现**：优先级调度、多级反馈队列、CFS、实时调度（EDF/RM）都没有涉及。
3. **用户态进程与地址空间隔离**：lab4 只创建内核线程，`copy_mm` 是空壳，没有 `fork` 一个真正独立的用户地址空间。
4. **进程同步与通信**：信号量、管程、条件变量、死锁避免（lab7 才有）。
5. **上下文切换的开销分析**：实验关心“能不能切对”，没有测切换耗时、缓存/TLB 抖动、调度器的吞吐与延迟。
6. **进程的完整生命周期**：`do_exit` 在 lab4 里直接 `panic`，没有僵尸进程回收、没有 `wait` 语义。
7. **多核（SMP）调度**：实验是单 hart，没有负载均衡、CPU 亲和性、IPI 唤醒。

### AI 协作开发的经验

1. **把“机制事实”写进 `[RELY]`，比写十句“请仔细”更有效。** 例如“`context.ra` 是 `forkret`”“`pgdir` 是物理地址”“`lsatp()` 是 Sv32 遗留”，这些都是 AI 靠猜会猜错、写进提示词就一次到位的东西。
2. **提示词要按“实现 / 验证 / 审查”拆开。** 一个提示词里既要功能正确、又要顺手写测试，AI 往往两头都做不扎实；拆开之后，问题能精确定位到“是代码错了”还是“验证方法错了”。
3. **AI 生成的汇编和页表代码，必须自己拿“机器码/页表项”对一遍。** 这次 `satp` 的拼法、`switch_to` 的寄存器清单，都是靠对着 `enable_paging()` 和 `struct context` 逐字核对才确认的。
4. **“本地跑不通”不等于“代码错了”。** 第三次迭代的教训很典型：先用 `-d int` 抓 trap、再用 gdb 读页表，才把“环境差异”和“实现 bug”分开；也因此没有为了让本地跑通去改 `Makefile` / `tools/`——那会埋下“课程环境反而挂”的雷。
5. **模块之间的接口要写下来。** 模块二把 `context.ra`/`context.sp`/`tf->epc` 摆好，模块三才能“跳过去”；这些约定不写清楚，联调时就会互相怀疑。
