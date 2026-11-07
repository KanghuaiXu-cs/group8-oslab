# lab3 功能模块一：Trap 上下文恢复与 Trap 入口

> 负责范围：`kern/trap/trapentry.S` 中的 `RESTORE_ALL` 与 `__alltraps`。
> 不改动 `SAVE_ALL`、`struct trapframe`、`trap()` 的函数签名。

---

## 0. 先回答：模块一是模块二的基础吗？

**是，模块一是模块二的基础；但两者是“同一个 Trap 闭环的两半”，不是“做完一个再开始下一个”的串行关系。**

### 0.1 依赖链条（谁调用谁）

```
硬件 Trap
  └─> __alltraps            ← 模块一
        ├─ SAVE_ALL          （框架已给）把上下文存成 trapframe
        ├─ mv a0, sp         ← 模块一：tf = 当前栈顶的 trapframe
        └─ jal trap          ← 模块一：按 RISC-V 调用约定进入 C
              └─ trap(tf)
                    └─ trap_dispatch(tf)
                          ├─ interrupt_handler(tf)   ← 模块二
                          └─ exception_handler(tf)   ← 模块二
        └─ __trapret
              ├─ RESTORE_ALL ← 模块一：从同一个 trapframe 恢复
              └─ sret
```

由这条链可以看出三件事：

1. **模块二的入参 `struct trapframe *tf` 是模块一交给它的。** `void interrupt_handler(struct trapframe *tf)` 里的 `tf`，就是 `__alltraps` 里用 `mv a0, sp` 传下去的那个指针。模块一如果没把 trapframe 建对、没按调用约定传参，模块二拿到的就是垃圾数据（`tf->cause`、`tf->epc` 全是错的），它写什么都救不回来。
2. **模块二改的 `tf->epc`，必须靠模块一的 `RESTORE_ALL` 才会生效。** 模块二处理断点异常时把 `tf->epc += 2`，这个修改只有在 `RESTORE_ALL` 里执行 `csrw sepc, s2`（从 `tf->epc` 读回 `sepc`）之后才真正作用到硬件上；否则 `sret` 还是回到原指令，形成异常死循环。
3. **模块二处理完必须能“返回”。** `interrupt_handler`/`exception_handler` 正常返回后，控制流回到 `trap()` → `__alltraps` 的下一条 → `__trapret` → `RESTORE_ALL` → `sret`。模块一这里如果恢复错一个寄存器或栈指针，模块二再正确也会在返回时崩掉。

### 0.2 但在“开发”和“评分”上，两边互相牵着

- **模块一可以独立开发、独立自测。** 因为它对外只表现为“进入时保存、退出时恢复”，可以用两个空壳 handler 先把内核链接起来、单独验证“陷入→保存→返回”这条通路（本报告的验证就是这么做的：临时桩函数 + 官方测试）。
- **但官方 Test 1（35 分）的判定条件同时落在两个模块上：**
  - `grade.sh` 对 Test 1 要求输出里既有 `Breakpoint at ...`（**模块二**的 `exception_handler` 打印），又有 `[Test 1] Trap entry and breakpoint return: PASS`（**模块一**的上下文能否原样恢复）。
  - 断点异常的 `epc` 修正（模块二）只有在模块一的 `RESTORE_ALL` 恢复 `sepc` 之后才生效；而 `epc` 一旦修正错（比如 `ebreak` 是 2 字节却加了 4），返回后又会落进异常，Test 1 同样失败。
- **模块二离开了模块一根本拿不到正确的 `tf`。** 所以“模块二在模块一的基础上完成”这个说法是对的。

### 0.3 给小组的分工建议

先口头（或写进 README）约定这三条接口，之后各写各的就不会打架：

| 约定项 | 内容 | 由谁保证 |
| --- | --- | --- |
| trapframe 布局 | `x0..x31` 依次占 `0..31*8`；`status=32*8`、`epc=33*8`、`badvaddr=34*8`、`cause=35*8`（`REGBYTES=8`） | 双方共同遵守，谁都不改 `struct trapframe` |
| sp 的位置 | 槽位 2 存的是**被打断时的 sp**（`SAVE_ALL` 经 `sscratch` 中转），不是 `sp-36*8` | 模块一 |
| epc 的修改权 | 模块二只改 `tf->epc`，**不碰** `trapentry.S`；模块一只负责把 `tf->epc` 写回 `sepc` | 模块一 + 模块二 |

---

## 1. 模块一要实现什么

| 符号 | 位置 | 职责 |
| --- | --- | --- |
| `RESTORE_ALL` | `trapentry.S` | 输入当前 trapframe，输出“被打断程序的处理器状态”：写回 `sstatus`/`sepc`，按 `SAVE_ALL` 的布局原样恢复 31 个通用寄存器，最后恢复 `sp` |
| `__alltraps` | `trapentry.S` | `SAVE_ALL` 之后把 trapframe 地址按 RISC-V 调用约定（`a0` = 第一个参数）传给 `trap()` |

---

## 2. 实现

```asm
    .macro RESTORE_ALL
    #lab3 function1 YOUR CODE
    # 1. restore the CSRs first.
    LOAD s1, 32*REGBYTES(sp)        # sstatus
    LOAD s2, 33*REGBYTES(sp)        # sepc
    csrw sstatus, s1
    csrw sepc, s2

    # 2. restore general purpose registers, exactly in the layout that
    # SAVE_ALL stored them in.
    # x0 (slot 0) is hard-wired zero, nothing to do.
    # x2 (sp, slot 2) is NOT restored here, see step 3.
    LOAD x1,  1*REGBYTES(sp)
    LOAD x3,  3*REGBYTES(sp)
    ...                                  # x3 ~ x31 逐条恢复
    LOAD x31, 31*REGBYTES(sp)

    # 3. restore sp LAST
    LOAD x2,  2*REGBYTES(sp)
    .endm

    .globl __alltraps
    .align(2)
__alltraps:
    SAVE_ALL

    #lab3 function1 YOUR CODE
    # sp should be the same as before "jal trap"
    mv a0, sp          # tf = trapframe
    jal trap           # 调用 C 处理函数；ra 被覆盖也无所谓，槽位 1 存着原 ra

    .globl __trapret
__trapret:
    RESTORE_ALL
    sret
```

（完整版见 `kern/trap/trapentry.S`，其中 `x3~x31` 逐条展开。）

---

## 3. 三个关键点：为什么必须这样写

### 3.1 为什么 `sstatus` / `sepc` 要先恢复，而且要借 `s1`/`s2` 当中转

`RISC-V` 不能直接把 CSR 写到内存，也不能“从内存直接写 CSR”，中间必须过一个通用寄存器。`SAVE_ALL` 里用的是 `s1`（x9）、`s2`（x18）做中转；`RESTORE_ALL` 同理，先 `LOAD s1/s2 ← 32/33 号槽`，再 `csrw`。

**注意：这两个指令会覆盖 x9、x18。** 所以顺序必须是：

1. 先把 CSR 写回去（此时 `s1`/`s2` 已经没用）；
2. 再把 x1、x3~x31 从内存恢复 —— 其中就包括 x9、x18。

如果反过来（先恢复通用寄存器、再想写 CSR），就得再借别的寄存器，越写越乱。这也就是下面 3.3 那个 bug 的根源。

### 3.2 为什么 `sp` 必须最后恢复（与 `SAVE_ALL` 对 x2 的特殊处理对应）

`SAVE_ALL` 里 x2 并没有像其它寄存器那样直接 `STORE x2`：

```asm
    csrw sscratch, sp        # 先把“被打断时的 sp”存进 sscratch
    addi sp, sp, -36 * REGBYTES
    ...
    csrrw s0, sscratch, x0   # s0 ← 原 sp，同时把 sscratch 清 0
    STORE s0, 2*REGBYTES(sp) # 把“原 sp”存进槽位 2
```

也就是说，**槽位 2 里放的是“进 trap 之前的 sp”，不是“trapframe 的基址”**。因此：

- `RESTORE_ALL` 在恢复 x2 之前，所有 `LOAD` 都要以当前 sp（= trapframe 基址）为基址，所以 **x2 只能最后恢复**；
- 最后一条 `LOAD x2, 2*REGBYTES(sp)` 一举两得：既恢复了原来的 sp，又顺带把整个 trapframe 弹出栈（不需要再 `addi sp, sp, 36*REGBYTES`）。

**这里有个容易踩的坑**：`addi sp, sp, 36*REGBYTES` 和 `LOAD x2, 2*REGBYTES(sp)` 在数值上等价，但**不能两个都写**。两个都写，sp 会多加 288 字节，返回后必然跑飞。二选一，本实现选后者（与 `SAVE_ALL` 的“经 `sscratch` 中转”一一对应，语义更一致）。

### 3.3 为什么 `jal trap` 覆盖 `ra` 没有关系

`jal` 会把返回地址写进 `ra`（x1），看起来破坏了被打断程序的 `ra`。但 `SAVE_ALL` 早已把原 `ra` 存进槽位 1，`RESTORE_ALL` 最后会把 x1 恢复回来，所以被打断的程序观察不到任何变化。

`sp` 在 `trap()` 调用前后不变（`a0` 是参数、`sp` 是 callee 的栈，`trap()` 返回后 `sp` 仍指向同一个 trapframe），所以 `__trapret` 里的 `RESTORE_ALL` 拿到的基址仍然正确 —— 这正是代码里那句注释 `# sp should be the same as before "jal trap"` 的含义。

---

## 4. 提示词（按实验要求：先设计 Prompt，再让 AI 实现）

实验要求“设计提示词借助 AI 完成”，所以报告里要写清 spec 和 prompt。下面是我实际使用的结构。

### 4.1 Spec（先写清楚约束，再交给 AI）

- **输入**：`kern/trap/trapentry.S` 里已有的 `SAVE_ALL` 宏、`struct trapframe` 定义（`kern/trap/trap.h`）、`__trapret` 与 `sret` 的既有位置。
- **输出**：`RESTORE_ALL` 宏 + `__alltraps` 中 `SAVE_ALL` 之后的两三行。
- **硬约束**：
  1. 不得修改 `SAVE_ALL`、`struct trapframe`、`void trap(struct trapframe *tf)` 的签名；
  2. `RESTORE_ALL` 必须与 `SAVE_ALL` 的布局严格一一对应（36 个槽位 × `REGBYTES`）；
  3. 必须处理 `SAVE_ALL` 对 x2 的特殊处理（槽位 2 存的是原 sp）；
  4. 必须让 `trap()` 按 RISC-V 调用约定拿到 `struct trapframe *`；
  5. 只允许用 `riscv.h` 里已有的 `LOAD`/`STORE`/`REGBYTES` 宏，保持 32/64 位通用；
  6. 返回路径必须能通过 `sret` 回到原执行流，且 `trap()` 对 `tf->epc` 的修改要生效。
- **验收**：`make` 通过；`make grade` 的 Test 1 通过；时钟中断能连续触发（说明“陷入→返回”可重复）。

### 4.2 Prompt（实际给 AI 的文本，可直接复用）

```
你在完成 riscv64-ucore 的 lab3 功能模块一，只改 kern/trap/trapentry.S。

背景（已有代码，不可修改）：
- SAVE_ALL 宏把 36 个 8 字节槽位压在当前内核栈上，形成 struct trapframe：
  槽位 0..31 依次是 x0..x31；槽位 32=sstatus，33=sepc，34=sbadaddr，35=scause。
- 注意 SAVE_ALL 对 x2(sp) 的处理与其它寄存器不同：
  它先把原 sp 写进 sscratch，减栈后再用 csrrw s0, sscratch, x0 把原 sp 取回，
  最终存到槽位 2。因此槽位 2 存的是“进 trap 之前的 sp”。
- 32 个通用寄存器的槽位与 x0..x31 的编号一一对应。
- trapframe 的地址就是 SAVE_ALL 执行完之后的 sp。

任务：
1) 实现 RESTORE_ALL 宏：
   - 从槽位 32/33 恢复 sstatus、sepc（用 s1/s2 做中转，因为它们稍后会从内存恢复）；
   - 按 SAVE_ALL 的布局恢复 x1、x3..x31；
   - 最后恢复 x2(sp)，且必须最后恢复（前面的 LOAD 都以当前 sp 为基址）。
2) 在 __alltraps 的 SAVE_ALL 之后补上：把 trapframe 指针按 RISC-V 调用约定
   传给 C 函数 void trap(struct trapframe *tf)，然后落到 __trapret。

约束：
- 不能修改 SAVE_ALL、struct trapframe、trap() 签名；
- 不要写 addi sp, sp, 36*REGBYTES（与 LOAD x2 重复，会多弹 288 字节）；
- 只用 riscv.h 里的 LOAD/STORE/REGBYTES；
- 注释用中文，说明每一步在恢复什么。
请只输出这两段的最终汇编。
```

### 4.3 AI 给出的结果，我是怎么审的

AI 第一版写对了主体结构，但有两处需要我自己判断：

1. 它把 `RESTORE_ALL` 写成“先 `addi sp, sp, 36*REGBYTES`，再按偏移恢复”的形式 —— 看似聪明（把基址直接挪回原 sp），但和槽位 2 的语义重复，且一旦和 `LOAD x2` 同时存在就多弹一次；我改回了“槽位 2 最后 `LOAD`”的写法。
2. 它把恢复 CSR 放在了恢复通用寄存器之后 —— 这样 x9/x18 已经被覆盖，`s1/s2` 里不再是 `sstatus/sepc`。必须**先写 CSR，再恢复通用寄存器**。

这两点都是“布局一致性”问题，光看伪代码看不出来，必须对着 `SAVE_ALL` 的槽位表逐条核对。

---

## 5. 踩过的坑（按报告要求：bug 与解决）

| # | 现象 | 原因 | 解决 |
| --- | --- | --- | --- |
| 1 | Test 1 输出 `FAIL (register context)` | `s1`(x9)、`s2`(x18) 被当成 `sstatus`/`sepc` 的中转寄存器，恢复通用寄存器时漏了 x9（以为“s 开头的不用管”） | 逐条核对 `SAVE_ALL` 的 31 条 `STORE`，`LOAD` 一条不少；把“先写 CSR、后恢复 GPR”的顺序写进注释 |
| 2 | 返回时打出 `trapframe` 且 `cause 0x00000007`，之后死循环 | 把 `LOAD x2, 2*REGBYTES(sp)` 提到了 `RESTORE_ALL` 开头，后面所有 `LOAD` 的基址变成了“原 sp”，读到的是栈上的垃圾 | `x2` 严格最后恢复；并在注释里写明“所有 LOAD 都以当前 sp 为基址” |
| 3 | `sret` 后重复执行同一条 `ebreak` | 以为“异常处理里改 `tf->epc` 就够了”，忽略了它要由 `RESTORE_ALL` 的 `csrw sepc` 才生效 | `RESTORE_ALL` 必须恢复 `sepc`（槽位 33） |
| 4 | `addi sp, sp, 36*REGBYTES` 和 `LOAD x2` 同时存在时返回后跑飞 | 弹栈两次 | 只保留一种；本实现只 `LOAD x2` |

> 第 1、2 条我专门做了“变异实验”反向验证：故意删掉 `LOAD x9`，测试立刻变成 `FAIL (register context)`；故意把 `LOAD x2` 提前，立刻变成 `cause 0x7` 的异常循环。这说明测试确实能测出问题，PASS 不是“碰巧”。

---

## 6. 验证记录

环境：WSL Ubuntu + `riscv-none-elf-gcc 13.3.0`（xpack）+ `qemu-system-riscv64 10.2`，用 `tools/inject.py` 注入官方测试，编译时加 `-DGRADE_LAB3_CONTEXT` / `-DGRADE_LAB3_TIMER`。

| 测试 | 命令 | 结果 |
| --- | --- | --- |
| Test 1（trap 入口 + 寄存器恢复 + 断点返回，35 分） | `make DEFS+=-DGRADE_LAB3_CONTEXT` | `Breakpoint at 0xffffffffc0201a4c` → `[Test 1] Trap entry and breakpoint return: PASS (35/35)` |
| Test 3（时钟中断持续触发，200 次陷入/返回，40 分） | `make DEFS+=-DGRADE_LAB3_TIMER` | `[Test 3] Sustained supervisor timer: PASS (40/40, ticks=200)`，并持续打印 `100 ticks` |
| 变异：删掉 `LOAD x9` | 同上 Test 1 | `[Test 1] ... FAIL (register context)`（说明测试有效） |
| 变异：`LOAD x2` 提前 | 同上 Test 1 | `cause 0x00000007` 异常循环（说明顺序是硬约束） |

**说明**：这两条测试都用了临时的模块二桩函数（只打印/推进 `epc`/重设时钟），仅为把内核链接起来验证模块一；桩函数不在交付代码里，`kern/trap/trap.c` 的 `//lab3 function2 YOUR CODE` 原样保留给模块二。

---

## 7. 给负责模块二的同学（三个容易踩的坑）

1. **`tf->cause` 的最高位是 1 表示“中断”**。硬件给的 `scause` 是 `(1<<63) | 中断号`，直接 `switch (tf->cause) { case IRQ_S_TIMER: ... }` 永远匹配不上（我第一版桩函数就栽在这，输出 `Unknown interrupt: cause 5` 刷屏）。要先判最高位，或对低 8 位取掩码，例如 `switch (tf->cause & 0xff)`。
2. **`ebreak` 在本实验的工具链下是 2 字节的 `c.ebreak`（机器码 `0x9002`）**，不是 4 字节。官方 Test 1 用的是压缩后的 `ebreak`，断点异常必须 `tf->epc += 2`；而 Test 2 的非法指令是 `.word 0x0000007b`（4 字节），要 `+= 4`。**建议按指令低两位判长度**：`(*(uint16_t *)tf->epc & 3) == 3 ? 4 : 2`，别硬编码。
3. **`grade.sh` 是靠字符串匹配判分的**，这些输出一个字都不能差：
   - Test 1：`Breakpoint at`
   - Test 2：`Illegal instruction at`、`Supervisor software interrupt`、`Unknown interrupt: cause 15`、`cause    0x00000014`（`print_trapframe` 的输出格式）
   - Test 3：`100 ticks`、`ticks=200`
   另外时钟中断**每次都要 `clock_set_next_event()`**，只计数不重设就只来一次中断。

---

## 8. 知识点对照（报告要求的那一节）

| 本实验的知识点 | OS 原理中的对应 | 关系与差异 |
| --- | --- | --- |
| `SAVE_ALL` / `RESTORE_ALL` 保存恢复 36 个槽位 | 进程上下文（context）与上下文切换 | 原理里的上下文切换通常指“进程↔进程”；本实验是“同一执行流 ↔ 中断处理程序”的**同一进程内**上下文保存，没有 PCB、没有调度 |
| `struct trapframe` 放在内核栈顶 | 中断帧 / 内核栈 | 原理讲“中断时硬件保存少量状态、软件保存其余”；这里把 32 个 GPR + 4 个 CSR 全部软件保存，是简化版 |
| `a0 = tf` + `jal trap` | 函数调用约定（ABI） | 原理里的 ABI 是编译器的事；这里是手写汇编去“假装”一次 C 调用，必须自己保证 `a0`/`sp`/`ra` 的约定 |
| `csrw sstatus/sepc` + `sret` | 特权级切换 / 中断返回 | 原理讲“返回时恢复 PSW 和 PC”；这里 `sret` 用 `sstatus.SPP` 决定回到哪个特权级，用 `sepc` 决定回到哪条指令 |
| `sscratch` 中转 x2 | （原理中通常不讲） | 属于 RISC-V 特有的“没有硬件自动压栈”的补救手段：没有 `sscratch` 就没法在保存 sp 之前拿到原 sp |

**原理中有、本实验没对上的**：中断优先级与嵌套中断的完整处理、中断控制器的编程（PLIC）、进程调度与上下文切换的代价分析、用户态/内核态栈的分离（本实验全部发生在 S 态，`sscratch` 的“用户态栈”用途没体现）。

---

## 9. 附：本地跑通的方法（环境说明）

本机这套工具链有两个和课程服务器不一致的地方，验证时绕开了，**与代码正确性无关**：

1. **QEMU 10.2 + OpenSBI 1.8 下 `-device loader` 的 Next Address 是 0**，内核不会被启动；改用 `-kernel bin/ucore.img`（lab1 时已经踩过一次）。
2. **QEMU ≥ 9 在 `csrw satp` 处会结束当前翻译块**，而 `kern_entry` 是在物理地址上开分页、下一条指令还在物理地址上取指（`0x80200038`），因此会立刻触发 instruction page fault。课程环境的 QEMU 较旧、不会这样。本地验证时我在 `boot_page_table_sv39` 里额外加了一个恒等映射（`VA 0x8000_0000 → PA 0x8000_0000`）绕过去；**这个改动只在本地验证副本里，交付代码没有改 `entry.S`**。

构建命令（WSL，工具链解压在 `~/tc`）：

```bash
export PATH=$HOME/tc/xpack-riscv-none-elf-gcc-13.3.0-2/bin:$PATH
make GCCPREFIX=riscv-none-elf- "DEFS=-march=rv64gc_zicsr -mabi=lp64d"
```

> 课程环境用 `riscv64-unknown-elf-gcc` 即可，不需要上面这两个参数；这里加 `-march/-mabi` 是因为 xpack 工具链默认是 rv32.

---

## 10. 提交前检查清单

- [ ] `kern/trap/trapentry.S` 中的 `#lab3 function1 YOUR CODE` 已把 `YOUR CODE` 换成**学号**（实验报告要求）。
- [ ] `kern/mm/pmm.c`、`kern/mm/best_fit_pmm.c` 的 lab2 代码已填回（否则整个内核链接不过，`make` 直接失败）。
- [ ] `kern/trap/trap.c` 的 `//lab3 function2 YOUR CODE` 保留，等模块二填。
- [ ] `make` 无 error（模块二填完后）；`make grade` 能看到 `[Test 1] ... PASS`。
