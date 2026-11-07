# 实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 3：中断与异常处理 |
| **小组成员** | 2510789-徐康怀、2514022-邓翔予、2510538-张铭瑞 |
| **完成日期** | 2026-11-07 |

### 小组分工

练习/模块分工：

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2510789-徐康怀 | 功能模块一：Trap 上下文恢复与入口（`RESTORE_ALL`、`__alltraps`） |
| 2514022-邓翔予 | 功能模块二：中断处理（`interrupt_handler`，时钟中断） |
| 2510538-张铭瑞 | 功能模块二：异常处理（`exception_handler`、`insn_len`） |

实验报告分工：

| 成员 | 负责章节 |
|------|----------|
| 2510789-徐康怀 | 4.1 功能模块一；5.2 上下文/断点测试 |
| 2514022-邓翔予 | 4.2.1 中断处理；5.4 持续时钟测试 |
| 2510538-张铭瑞 | 三、整体逻辑；4.2.2 异常处理；4.3 提示词与迭代；5.1/5.3/5.5～5.7；六、总结 |

> 说明：4.1、4.2.1 的“最终提示词/实现迭代过程”由对应负责人补充，本文暂记录接口、当前集成行为和测试结果。

---

## 一、实验目的

本实验的主要目的是：

1. 理解 RISC-V 的 Trap 机制：中断（interrupt）与异常（exception）的区别，`scause`、`sepc`、`stvec`、`sstatus` 等 CSR 在 Trap 中的作用。
2. 理解 Trap 上下文的保存与恢复：`struct trapframe` 的布局、`SAVE_ALL`/`RESTORE_ALL` 的对应关系，以及为什么 `sp` 要特殊处理。
3. 理解 Trap 入口与返回的完整闭环：`__alltraps` → `trap()` → `trap_dispatch()` → `exception_handler()`/`interrupt_handler()` → `__trapret` → `sret`。
4. 掌握同步异常的处理方法：非法指令和断点异常发生后，如何根据指令长度修正 `epc`，使 `sret` 返回后不会重复执行同一条指令。
5. 掌握时钟中断的基本处理：重设下一次时钟事件、累计 `ticks`、按 `TICK_NUM` 输出统计信息、累计到 `SHUTDOWN_AFTER` 后关机。
6. 练习“先写 Spec、再写 Prompt、让 AI 生成、自己验证”的协作方式。

---

## 二、实验环境

| 组件 | 版本/说明 |
|---|---|
| 宿主系统 | Windows 11 + WSL2 |
| Linux 发行版 | Ubuntu 24.04 |
| 交叉编译器 | `riscv64-unknown-elf-gcc 13.2.0` |
| 模拟器 | 张铭瑞：QEMU 8.2.2；徐康怀：QEMU 4.1.1（现场演示） |
| 固件 | OpenSBI v1.3（`-bios default`） |
| 构建方式 | `make` / `make qemu` |

AI 工具：

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2510789-徐康怀 | 待组员补充 | 待组员补充 | — |
| 2514022-邓翔予 | 待组员补充 | 待组员补充 | — |
| 2510538-张铭瑞 | DeepSeek Harness | deepseek-flash | 用于异常处理代码、测试与报告整理 |

> QEMU 版本差异：本机 8.2.2 的 `make grade` 使用 `-device loader` 时，OpenSBI 的 `next_addr` 为 0，CPU 会从 0 取指；改用 `-kernel` 启动同一套注入测试则全部通过。现场演示以徐康怀的 QEMU 4.1.1 为准。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

Lab1 让内核能启动，Lab2 让内核能管理物理内存，但这两章的内核都只能沿着一条直线往下跑：它不会响应外部设备，也无法在指令出错时接管现场。

Lab3 要补的是：

> **当 CPU 被某个事件打断时，操作系统如何保存现场、判断原因、处理事件，再恢复现场回到原处继续执行。**

RISC-V 把中断和异常统称为 Trap。Trap 发生时，硬件只做最少的事：

- 记录原因码到 `scause`；
- 记录出错/被打断的地址到 `sepc`；
- 把当前特权级和中断使能状态保存到 `sstatus`；
- 跳到 `stvec` 指向的入口。

剩下的“保存哪些寄存器、怎么分类、怎么处理、怎么返回”全部由软件完成。

所以整章可以概括成三句话：

1. **先定义现场**：`struct trapframe` 是 36 个 8 字节字段，汇编和 C 必须共用同一套布局。
2. **再打通进出通道**：`__alltraps` 保存现场并调用 `trap()`，`__trapret` 恢复现场并 `sret`。
3. **最后在通道中间挂处理逻辑**：`trap_dispatch()` 根据 `cause` 最高位分流到中断或异常处理。

### 3.2 功能的逐步实现

1. **先实现上下文恢复（`RESTORE_ALL`）**  
   为什么先做这个？因为 `SAVE_ALL` 已经把现场压栈，但如果恢复顺序错了，C 处理函数返回后回不到原程序；`sp` 必须最后恢复，否则前面所有 `LOAD` 都会读错位置。

2. **再实现 Trap 入口（`__alltraps`）**  
   为什么接着做？`SAVE_ALL` 之后要把 `sp` 作为 `trapframe *` 传给 `trap()`；处理完再进入 `__trapret`。入口和返回是一对，必须一起打通。

3. **然后实现异常处理（`exception_handler`）**  
   为什么接着做？入口/返回通了之后，同步异常才有意义。非法指令和断点是最好构造、最容易验证的两类异常：执行一条 `ebreak` 或一个非法编码就能触发。

4. **再实现中断处理（`interrupt_handler`）**  
   为什么接着做？时钟中断是异步事件，能验证“处理完还能回到原程序继续跑”，也能验证 `epc` 不需要像异常那样推进。

5. **最后用测试验收**  
   上下文测试验证入口/恢复；非法指令测试验证 `+4` 和兜底；时钟测试验证持续返回；`c.ebreak` 补验 `+2`。

一句话：**先把现场定义清楚，再把进出通道打通，最后才在通道中间挂分类处理逻辑。**

---

## 四、实验内容与实现

### 4.1 功能模块一：Trap 上下文恢复与入口

**负责人：** 2510789-徐康怀

#### 模块功能描述

**需要实现/修改的函数：**

```c
// kern/trap/trapentry.S
RESTORE_ALL;      // 汇编宏
__alltraps;       // Trap 入口
__trapret;        // Trap 返回
```

**功能说明：**

- `__alltraps` 是 `stvec` 指向的入口：先 `SAVE_ALL`，把 32 个通用寄存器和 4 个 CSR 压成 `trapframe`，再把 `sp` 作为参数调用 `trap()`。
- `__trapret` 在 `trap()` 返回后执行 `RESTORE_ALL`，最后 `sret`。
- `RESTORE_ALL` 的顺序：先恢复 `sstatus`/`sepc`，再恢复通用寄存器，最后恢复 `sp`。
- `sp` 特殊：`SAVE_ALL` 通过 `sscratch` 保存进入 Trap 前的旧 `sp`，所以 `RESTORE_ALL` 要从 trapframe 的偏移 `2 * REGBYTES` 取它，不能提前恢复。

#### 最终提示词

> 由徐康怀补充。

#### 实现迭代过程

> 由徐康怀补充。

---

### 4.2 功能模块二：中断与异常处理

本模块的入口是框架提供的：

```c
static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        interrupt_handler(tf);   // 最高位 1：中断
    } else {
        exception_handler(tf);   // 最高位 0：异常
    }
}
```

`scause` 最高位是中断标志：1 表示中断，0 表示异常。转成有符号的 `intptr_t` 后，最高位为 1 就是负数，所以可以直接用 `< 0` 判断。

#### 4.2.1 中断处理

**负责人：** 2514022-邓翔予

**需要实现/修改的函数：**

```c
void interrupt_handler(struct trapframe *tf);
```

**功能说明：**

- 处理 S 模式时钟中断（`IRQ_S_TIMER = 5`）：
  - 调用 `clock_set_next_event()` 重设下一次时钟事件；
  - `ticks++`；
  - 每 `TICK_NUM = 100` 次打印一次 `100 ticks`；
  - 累计 `SHUTDOWN_AFTER = 10` 次后调用 `sbi_shutdown()`。
- 处理软件中断（`IRQ_S_SOFT = 1`）和外部中断（`IRQ_S_EXT = 9`）的提示输出。
- 其他未知中断打印 cause，并调用 `print_trapframe(tf)` 兜底。

**最终提示词：**

> 由邓翔予补充。

**实现迭代过程：**

> 由邓翔予补充。

#### 4.2.2 异常处理（本人负责，重点）

**负责人：** 2510538-张铭瑞

##### 模块功能描述

**需要实现/修改的函数：**

```c
static inline size_t insn_len(uintptr_t epc);
void exception_handler(struct trapframe *tf);
```

**功能说明：**

`exception_handler()` 由 `trap_dispatch()` 在同步异常时调用，负责：

1. 非法指令异常（cause=2）：打印 `Illegal instruction at 0x...`，并根据指令长度推进 `tf->epc`；
2. 断点异常（cause=3）：打印 `Breakpoint at 0x...`，同样推进 `tf->epc`；
3. 其他异常：调用 `print_trapframe(tf)` 打印完整现场，不推进 `tf->epc`。

`insn_len()` 是只在本文件使用的辅助函数：读 `epc` 处 2 字节，低 2 位为 `0b11` 返回 4，否则返回 2。

##### 数据结构

核心数据结构是 `struct trapframe`：

```c
struct pushregs {
    uintptr_t zero, ra, sp, gp, tp;
    uintptr_t t0, t1, t2;
    uintptr_t s0, s1;
    uintptr_t a0, a1, a2, a3, a4, a5, a6, a7;
    uintptr_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uintptr_t t3, t4, t5, t6;
};

struct trapframe {
    struct pushregs gpr;   // 32 × 8 = 256 字节
    uintptr_t status;      // 256
    uintptr_t epc;         // 264
    uintptr_t badvaddr;    // 272
    uintptr_t cause;       // 280
};                         // 总大小 288 字节
```

`tf` 是指向栈上 trapframe 的指针，不是拷贝。`exception_handler` 只读 `tf->cause`、读写 `tf->epc`；`print_trapframe` 会读所有字段。

##### Spec 的写法

这次没有直接对 AI 说“实现 `exception_handler`”，而是先把需求拆成四段：

| 段落 | 写法 | 作用 |
|---|---|---|
| `[PROMPT]` | 指定文件、函数、调用者、职责 | 防止 AI 改错文件、改错函数 |
| `[RELY]` | 抄真实定义：`struct trapframe` 字段、`CAUSE_*` 的值、`print_trapframe`、`cprintf`、`sepc/sret`、指令编码规则 | AI 不会自动知道这个工程的布局，不能让它猜 |
| `[GUARANTEE]` | 写硬约束：签名、非 `static`、只改 `tf->epc`、输出关键字、`-Werror` | 把“必须做到”与“不能做”分开 |
| `[SPECIFICATION]` | 写行为、失败路径、边界、验收 | 让代码可以被测试，而不是“看起来对” |

最关键的是两条边界：

- **16 位压缩指令**：低 2 位不是 `0b11`，所以不能固定 `+4`；
- **未知异常**：只打印、不推进 `epc`，不能把没有修复的异常静默跳过。

如果只写“实现异常处理”，AI 很可能生成“打印 + 固定 `+4`”的版本，这正是第一次迭代出现的问题。

##### 最终提示词

以下是实现该模块使用的最终提示词：

````markdown
[PROMPT]
在 ucore lab3 的 kern/trap/trap.c 中实现异常处理函数 exception_handler(struct trapframe *tf)。
它由 trap_dispatch() 在 scause 最高位为 0（同步异常）时调用，负责处理非法指令异常、断点异常，
并对其他异常做兜底输出。可以增加一个只在本文件使用的 static inline 辅助函数 insn_len()。
要求给出完整 C 代码、逐段解释，以及可执行的验证方法；不要修改 trap.c 中其他已有函数。

[RELY]
- struct trapframe 定义在 kern/trap/trap.h：
  struct pushregs gpr（32 个 uintptr_t）；uintptr_t status; uintptr_t epc;
  uintptr_t badvaddr; uintptr_t cause;
- 宏：CAUSE_ILLEGAL_INSTRUCTION = 0x2，CAUSE_BREAKPOINT = 0x3（libs/riscv.h）
- 函数：void print_trapframe(struct trapframe *tf);（kern/trap/trap.c）
- 输出：int cprintf(const char *fmt, ...);（libs/stdio.h）
- 机制：异常发生时硬件把 sepc 设为触发异常的指令地址；__trapret 执行 RESTORE_ALL 后 sret，
  sret 会跳回 sepc。因此处理函数修改 tf->epc 等价于修改返回地址。
- RISC-V 指令编码：32 位指令最低 2 位为 0b11；16 位压缩指令最低 2 位不是 0b11。
- 编译选项：-Wall -Werror，代码不能有警告。

[GUARANTEE]
- 交付函数：void exception_handler(struct trapframe *tf)，定义在 kern/trap/trap.c；
- 不要加 static：实验测试代码以 extern 方式引用该符号；
- 不修改函数签名，不修改 struct trapframe 布局，不修改 trap.c 中其他已有函数；
- 非法指令和断点异常：输出包含 "Illegal instruction at" / "Breakpoint at" 的信息，
  并把 tf->epc 推进到下一条指令；
- 其他异常：只调用 print_trapframe(tf) 兜底，不推进 tf->epc；
- 辅助函数 insn_len() 用 static inline，只在本文件可见；
- 代码要能通过 -Werror 编译。

[SPECIFICATION]
前置条件：
1. tf 指向 SAVE_ALL 建立的合法 trapframe；
2. tf->cause 是异常原因码（最高位为 0）；
3. tf->epc 指向触发异常的指令，且该指令所在页已映射、可读。

行为规格：
1. cause == 2（非法指令）：
   - 打印 "Illegal instruction at 0x%016lx\n" 和 tf->epc；
   - 读取 tf->epc 处 2 字节，若低 2 位 == 0b11，则 tf->epc += 4，否则 += 2；
2. cause == 3（断点）：
   - 打印 "Breakpoint at 0x%016lx\n" 和 tf->epc；
   - 按同样的规则推进 tf->epc；
3. 其他 cause：
   - 调用 print_trapframe(tf)，不修改 tf->epc；
4. 函数返回后，由 __trapret 的 RESTORE_ALL 把 tf->epc 写回 sepc，再 sret。

失败/边界：
- 若 tf->epc 指向的指令是 16 位压缩指令，必须只加 2；加 4 会落到指令中间；
- 若 cause 不是 2/3，不得推进 epc，否则会掩盖未修复的异常；
- 本实验不要求处理 48/64 位长指令，也不要求处理缺页等异常的恢复；
- 不得假设 tf->epc 一定 4 字节对齐（压缩指令只保证 2 字节对齐）；
- 不要读 4 字节来判断长度，只读 2 字节，避免跨页读到未映射页。

验收：
1. make 编译通过（-Werror）；
2. ebreak 测试：打印一次 "Breakpoint at ..."，返回后继续执行，不刷屏；
3. 非法指令测试：打印一次 "Illegal instruction at ..."，返回后继续执行；
4. 未知异常测试：打印 trapframe，cause 字段正确；
5. 若条件允许，用 c.ebreak 验证 16 位指令走 +2 分支。
````

> 完整提示词套件（主实现 / 测试生成 / 代码审查）见 [prompt.md](./prompt.md)。报告里只放主实现提示词。

##### 实现代码

```c
//lab3 function2 2510538

/* 一条指令占几个字节。
 * RISC-V 的 32 位指令低 2 位固定为 0b11，16 位压缩指令低 2 位一定不是 0b11。
 * 读 epc 处的半个字，看低 2 位就能判断，不需要完整解码。
 * 本实验只涉及 16/32 位指令；更长的编码不在处理范围内。 */
static inline size_t insn_len(uintptr_t epc) {
    return (*(const uint16_t *)(uintptr_t)epc & 0x3) == 0x3 ? 4 : 2;
}

/* 异常处理：非法指令、断点、其他。
 * 函数签名由实验框架规定；框架测试代码以 extern 方式引用，因此不能加 static。 */
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
    case CAUSE_ILLEGAL_INSTRUCTION:
        cprintf("Illegal instruction at 0x%016lx\n", tf->epc);
        // 跳过这条指令，否则 sret 回来会再触发同一异常，形成死循环
        tf->epc += insn_len(tf->epc);
        break;

    case CAUSE_BREAKPOINT:
        cprintf("Breakpoint at 0x%016lx\n", tf->epc);
        tf->epc += insn_len(tf->epc);
        break;

    default:
        // 实验未要求具体处理的异常：打印完整 trapframe 兜底，不推进 epc。
        print_trapframe(tf);
        break;
    }
}
```

##### 实现迭代过程

本模块经历了 3 次主要迭代。

###### 第一次迭代：固定 `+4`，遇到 `c.ebreak` 失败

**遇到的问题：**

- 第一版把所有异常都写成 `tf->epc += 4`。
- 用 32 位 `ebreak` 测试时正常，因为 `ebreak` 正好 4 字节。
- 换成 16 位 `c.ebreak` 后，输出变成：

```text
before c.ebreak
Breakpoint at 0xffffffffc02000a0
Illegal instruction at 0xffffffffc02000a4
Breakpoint at 0xffffffffc02000a0
Illegal instruction at 0xffffffffc02000a4
...
```

- 原因：`c.ebreak` 在 `0xa0`，只占 2 字节，下一条有效指令在 `0xa2`；`+4` 跳到 `0xa4`，落在 `0xa2` 那条 32 位指令的中间，解码成非法指令，形成循环。

**问题解决策略：**

- 在 `[SPECIFICATION]` 中增加“16 位压缩指令低 2 位不是 `0b11`”的边界；
- 增加 `insn_len()`：读 `epc` 处 2 字节，低 2 位 `0b11` 返回 4，否则返回 2；
- 用 `c.ebreak` 重新验证，输出 `before` → `Breakpoint at 0xa0` → `after`。

###### 第二次迭代：`static` 导致链接失败

**遇到的问题：**

- 把 `exception_handler` 写成 `static void exception_handler(...)`。
- 注入 `GRADE_LAB3_ILLEGAL` 测试后链接失败：

```text
kern/test/test_lab3.c:30:(.text.test_lab3_handler_fallbacks+0x2e):
    undefined reference to `exception_handler'
```

- 原因：测试代码在另一个编译单元里用 `extern` 引用它，`static` 让符号只在本文件可见。

**问题解决策略：**

- 去掉 `static`，保持外部链接；
- 在 `[GUARANTEE]` 中明确写“不要加 static”。

###### 第三次迭代：输出字符串与验收脚本不匹配

**遇到的问题：**

- 输出写成 `Breakpoint!` / `Illegal instruction!`。
- 测试函数内部仍然打印 PASS，但 `grade.sh` 的 sentinel 是 `"Breakpoint at"` / `"Illegal instruction at"`，匹配不到，整项判 0。

**问题解决策略：**

- 把输出改成 `Breakpoint at 0x...` / `Illegal instruction at 0x...`；
- 在 `[SPECIFICATION]` 的验收里写明输出必须包含的关键字。

###### 最终结果

经过 3 次迭代，该模块最终：

- 编译通过（`-Werror`）；
- 上下文/断点测试通过（35/35）；
- 非法指令/兜底测试通过（25/25）；
- 持续时钟测试通过（40/40，`ticks=200`）；
- 额外用 `c.ebreak` 验证了 `+2` 分支。

**关键改进点总结：**

1. 在 `[RELY]` 中补充了 RISC-V 16/32 位指令编码规则，避免 AI 按“一条指令 4 字节”猜；
2. 在 `[SPECIFICATION]` 中明确了“未知异常不推进 `epc`”的失败路径；
3. 在 `[GUARANTEE]` 中补充了“非 static”“输出关键字”“`-Werror`”三条硬约束。

---

### 4.3 模块之间的衔接

```text
异常/中断发生
  ↓
硬件：sepc、scause、sstatus 就绪，跳到 stvec
  ↓
__alltraps：SAVE_ALL，形成 trapframe，调用 trap(tf)
  ↓
trap() → trap_dispatch(tf)
  ↓
cause 最高位 1 → interrupt_handler（邓翔予）
cause 最高位 0 → exception_handler（张铭瑞）
  ↓
处理函数返回
  ↓
__trapret：RESTORE_ALL，把 tf->epc 写回 sepc，恢复寄存器
  ↓
sret：PC = sepc，继续执行
```

---

## 五、测试与验证

> 文本记录来自同一份代码在 QEMU 8.2.2 上的运行结果；现场演示由徐康怀在 QEMU 4.1.1 上完成。

### 5.1 编译与时钟中断

```bash
cd lab3
make clean
make
make qemu
```

关键输出（省略 OpenSBI banner）：

```text
memory management: best_fit_pmm_manager
physcial memory map:
  memory: 0x0000000008000000, [0x0000000080000000, 0x0000000087ffffff].
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0205000
satp physical address: 0x0000000080205000
++ setup timer interrupts
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
```

10 行 `100 ticks` 对应 `SHUTDOWN_AFTER=10`，每行间隔约 1 秒（`TICK_NUM=100`，每次 10 ms），说明时钟中断被持续重新设置。

### 5.2 上下文/断点测试

```text
Breakpoint at 0xffffffffc02019b4
[Test 1] Trap entry and breakpoint return: PASS (35/35)
```

验证点：

- `ebreak` 前设置的 26 个寄存器特征值，返回后逐个比较，全部一致；
- `RESTORE_ALL` 的偏移、顺序、`sp` 最后恢复都正确；
- `exception_handler` 对 32 位 `ebreak` 推进 4 字节，返回后不会重复执行。

### 5.3 非法指令与兜底测试

```text
Illegal instruction at 0xffffffffc0201966
Supervisor software interrupt
Unknown interrupt: cause 15
trapframe at 0xffffffffc0204eb0
...
  cause    0x00000014
[Test 2] handler fallback completed\n[Test 2] Illegal instruction return: PASS (25/25)
```

验证点：

- `.word 0x0000007b`（4 字节）触发非法指令，打印一次，`epc += 4` 后返回；
- `interrupt_handler()` 对软件中断、未知中断（15）有输出；
- `exception_handler()` 对未知异常（20）调用 `print_trapframe()`，输出 `cause 0x00000014`，不推进 `epc`。

> 输出里的 `\n` 是测试源码中的字面量，不是运行错误；sentinel 仍能匹配。

### 5.4 持续时钟测试

```text
++ setup timer interrupts
100 ticks
100 ticks
[Test 3] Sustained supervisor timer: PASS (40/40, ticks=200)
100 ticks
...
```

`ticks=200` 说明时钟中断不是只来一次，而是被 `clock_set_next_event()` 持续重新设置。

### 5.5 16 位压缩断点测试

为了验证 `insn_len()` 的 `+2` 分支，在 `kern_init()` 中临时插入：

```c
cprintf("before c.ebreak\n");
asm volatile(".option push\n.option rvc\nc.ebreak\n.option pop");
cprintf("after c.ebreak\n");
```

运行结果：

```text
before c.ebreak
Breakpoint at 0xffffffffc02000a0
after c.ebreak
```

反汇编：

```text
ffffffffc02000a0:  9002        ebreak        # c.ebreak，2 字节
ffffffffc02000a2:  ...                       # 下一条指令
```

`epc=0xa0`，返回后执行 `0xa2`，说明 `insn_len()` 返回 2，`tf->epc += 2` 正确。如果固定 `+4`，会跳到 `0xa4` 的指令中间，触发非法指令循环，永远到不了 `after`。

> 该测试只用于验证，验证后已从最终源码删除。

### 5.6 测试覆盖情况

| 测试 | 来源 | 覆盖 | 没覆盖 |
|---|---|---|---|
| 上下文/断点 | 框架 `test_lab3_context.S` + `test_lab3.c` | 入口/恢复、32 位 `ebreak`、`+4` | 16 位指令、未知异常 |
| 非法指令/兜底 | 框架 `test_lab3.c` | 32 位非法指令、`+4`、`default` 打印 | 真实缺页恢复 |
| 持续时钟 | 框架 `test_lab3.c` | 时钟重设、`ticks` 累计 | 外部设备中断 |
| `c.ebreak` | 本人临时测试 | 16 位压缩指令、`+2` | 寄存器恢复、48/64 位指令 |

### 5.7 关于 `make grade`

本机 QEMU 8.2.2 下，`make grade` 使用：

```bash
qemu-system-riscv64 ... -bios default -device loader,file=bin/ucore.img,addr=0x80200000
```

OpenSBI 的 `Domain0 Next Address` 是 `0x0`，CPU 从 0 取指，内核不打印，三项测试全 0。用等价的 `-kernel bin/ucore.img` 启动后，注入的三项测试全部通过。这是本机 QEMU 版本与课程脚本的启动方式差异，不是 `trap.c` 的逻辑错误；提交的 `tools/` 未改动。

---

## 六、实验总结与收获

### 6.1 对操作系统的理解

| 实验中的概念 | 对应的 OS 原理 | 含义、关系与差异 |
|---|---|---|
| Trap（中断 + 异常） | 中断/异常机制、陷入内核 | 实验用 `scause` 最高位区分；原理课通常把中断、异常、系统调用分开讲，RISC-V 统一为 Trap |
| `stvec` + `__alltraps` | 中断向量表、陷阱入口 | 实验用 Direct 模式，所有 Trap 共用一个入口；x86 有 IDT、不同向量对应不同处理程序 |
| `struct trapframe` + `SAVE_ALL`/`RESTORE_ALL` | 上下文保存/恢复、PCB 中的现场 | 实验保存的是一次 Trap 的现场，不是进程切换的完整上下文；真实 OS 还要保存页表基址、浮点寄存器、信号掩码等 |
| `sepc` / `sret` | 精确异常、返回地址 | 异常是同步的，`sepc` 指向出错指令；中断是异步的，`sepc` 指向被打断的指令。是否推进 `epc` 取决于“这条指令是否已经处理完” |
| `sstatus.SPP/SPIE/SIE` | 特权级切换、中断嵌套 | 实验只做 S 模式内的 Trap，没有 U 模式；真实 OS 还要处理用户态/内核态栈切换、嵌套中断 |
| `sscratch` | 内核栈/用户栈切换 | 实验用 `sscratch` 暂存旧 `sp`；真实 OS 常把 `sscratch` 指向内核栈，用于从 U 模式陷入时切换栈 |
| 时钟中断 + `sbi_set_timer` | 时钟驱动、时间片、抢占式调度 | 实验只做“计数 + 打印 + 关机”；真实 OS 会在时钟中断里递减时间片、触发调度 |
| 指令长度与 `epc` 修正 | 指令解码、精确异常 | 实验只处理 16/32 位；真实 CPU 还可能有 48/64 位指令、向量指令 |
| `print_trapframe` | 内核调试、崩溃现场 | 实验用它兜底；真实 OS 会打印寄存器、栈回溯、进程信息，甚至生成 core dump |

OS 原理中很重要、但本实验没有覆盖的知识点：

1. **用户态与内核态的切换**：本实验的 Trap 都发生在 S 模式，没有 U 模式程序，也没有 `ecall` 系统调用。
2. **进程与调度**：时钟中断只计数，没有 `schedule()`；真实 OS 会在时钟中断里更新时间片、选择下一个进程、切换页表和寄存器。
3. **缺页异常与按需分页**：本实验对缺页只打印 trapframe，不修复；真实 OS 要查页表、分配物理页、从磁盘换入、修改 PTE，再重新执行指令。
4. **嵌套中断与中断优先级**：本实验进入 Trap 后硬件清 `SIE`，处理期间不再响应中断；真实 OS 可能允许高优先级中断嵌套，需要 PLIC 和屏蔽策略。
5. **多核与每 CPU 状态**：本实验是单 hart；真实多核系统里，`sscratch`、`stvec`、`sp` 都是每 CPU 的，Trap 处理还要考虑核间同步。
6. **浮点/向量上下文**：`trapframe` 只保存 32 个通用寄存器；真实 OS 还要保存浮点寄存器、向量寄存器。
7. **信号产生与投递**：异常在真实 OS 里可能转化为信号（如 SIGILL、SIGTRAP），本实验只打印。
8. **外部设备中断的完整管理**：本实验只处理 S 模式时钟中断，没有外部设备中断的使能、屏蔽、优先级、EOI 流程。

### 6.2 AI 协作开发的经验

1. **Spec 的价值在“边界”和“失败路径”**：这次 `+4` 的 bug 不是 AI “不会写”，而是提示词没有把“16 位压缩指令”这个边界写进去。把边界写成可判定的规则（低 2 位是否为 `0b11`）后，代码才正确。
2. **`RELY` 必须写真实定义，不能凭印象**：`struct trapframe` 的字段顺序、`CAUSE_*` 的值、`sret` 的行为，都要从代码/手册里抄，不能写“大概是”。AI 不会自动知道这个工程的布局。
3. **验收标准要可执行**：不要写“输出正确信息”，而要写“输出包含 `Breakpoint at`，且只打印一次”。这样生成的代码才能被脚本/人工稳定验证。
4. **AI 的输出必须自己跑测试**：三项注入测试、`c.ebreak` 测试、反汇编对照，都是“验证 AI 产出”的一部分。只看代码“看起来对”不够，必须让 CPU 真的跑一遍。
5. **环境问题要和代码问题分开**：`make grade` 全 0 一开始很像代码 bug，但 `make qemu` 正常 + `-d int` 看到 `epc=0` 后，才能定位到是 `-device loader` 的启动方式问题。先分层（编译/启动/测试/代码），再逐层排除。
6. **不要替组员讲他们的模块**：答辩按分工来，自己只讲自己写过的函数；被问到别人的模块，说明“这部分是组员负责的”，不要硬答。
