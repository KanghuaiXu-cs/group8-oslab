# Lab3 异常处理提示词套件

> 使用顺序：先 1（实现）→ 再 2（测试）→ 再 3（审查）；如果审查发现问题，用 3 的输出回到 1 的 `[SPECIFICATION]` 补约束。

---

## 1. 主实现提示词

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

### 这份提示词的关键点

| 段落 | 为什么这么写 |
|---|---|
| `[PROMPT]` | 指定文件、函数、调用者、职责，避免 AI 改错文件或改错函数 |
| `[RELY]` | 把 trapframe 字段、宏、`sepc/sret`、指令编码这些真实事实给 AI，不然它会按“一条指令 4 字节”猜 |
| `[GUARANTEE]` | 把签名、非 `static`、输出关键字、`-Werror` 写成硬约束 |
| `[SPECIFICATION]` | 把 cause 2/3、其他 cause、16/32 位边界、验收标准写成可执行规则 |

---

## 2. 测试生成提示词

````markdown
[PROMPT]
针对已经实现的 exception_handler()，设计并生成三组验证，用来证明：
1. 32 位 ebreak 走 +4，并且能返回到下一条指令；
2. 16 位 c.ebreak 走 +2，并且能返回到下一条指令；
3. 未知异常只打印 trapframe，不推进 epc。
要求给出临时测试代码、插入位置、预期输出和判定方法；测试完成后要能删除，不影响最终提交。

[RELY]
- 测试框架：tools/test_lab3.c、tools/test_lab3_context.S、tools/inject.py 已经提供上下文测试和非法指令测试；
- struct trapframe 可以用 memset 清零，构造一个假现场；
- 真实现场可以用 asm volatile 在 kern_init() 中插入 ebreak / c.ebreak；
- 压缩指令需要 .option rvc；
- 输出用 cprintf；
- 反汇编用 riscv64-unknown-elf-objdump -d bin/kernel。

[GUARANTEE]
- 不修改 tools/ 下的框架测试；
- 临时测试只加在 kern/init/init.c 或一个临时副本里，验证后删除；
- 测试代码要清楚标注“临时验证”，不要混进最终 exception_handler；
- 给出预期输出和失败时的现象。

[SPECIFICATION]
测试 1（32 位 ebreak）：
- 插入 asm volatile("ebreak");
- 预期：打印一次 "Breakpoint at 0x..."，返回后继续执行后续 cprintf；
- 若刷屏：epc 没推进；若跳到错误地址：推进量错误。

测试 2（16 位 c.ebreak）：
- 插入：
  cprintf("before c.ebreak\n");
  asm volatile(".option push\n.option rvc\nc.ebreak\n.option pop");
  cprintf("after c.ebreak\n");
- 预期：before → Breakpoint at 0x... → after；
- 用 objdump 确认 c.ebreak 处是 2 字节，下一条指令地址 = 当前地址 + 2；
- 若不打印 after、出现 "Illegal instruction"：说明固定 +4，跳到了指令中间。

测试 3（未知异常）：
- struct trapframe tf; memset(&tf, 0, sizeof(tf)); tf.cause = 20;
- 直接调用 exception_handler(&tf)；
- 预期：打印 trapframe，cause 为 0x00000014，不访问 tf.epc（它为 0）。

覆盖说明：
- 明确写出没有覆盖 48/64 位指令、缺页恢复、用户态、嵌套中断。
````

---

## 3. 代码审查 / 修 bug 提示词

````markdown
[PROMPT]
以代码审查者的身份，逐条检查 exception_handler() 和 insn_len() 是否满足实验约束。
不要重写整个文件；按“问题、位置、原因、修法、验证”输出表格。
重点检查以下六项：
1. epc 是否按指令长度推进，而不是固定 +4；
2. exception_handler 是否被误写成 static；
3. 非法指令/断点的输出是否包含验收关键字；
4. default 是否只打印、不推进 epc；
5. cprintf 的格式串与参数类型是否匹配（%016lx 对应 64 位）；
6. 是否修改了 trap.c 中其他函数或 trapframe 布局。

[RELY]
- 实验要求：非法指令/断点按长度更新 tf->epc；其他异常 print_trapframe 兜底；
- 测试脚本 sentinel："Breakpoint at"、"Illegal instruction at"、"cause    0x00000014"；
- 编译选项：-Wall -Werror；
- 测试代码以 extern 方式引用 exception_handler。

[GUARANTEE]
- 输出问题表，每条包含：文件/行、现象、原因、建议修改、验证方法；
- 只给修改建议，不直接改无关函数；
- 如果代码没有问题，明确写“六项均通过”，不要为了凑问题硬编。

[SPECIFICATION]
逐项判定：
1. 固定 +4 → 16 位 c.ebreak 会跳到指令中间；应读 2 字节，低 2 位 0b11 返回 4，否则 2；
2. static → 链接时 undefined reference；应去掉 static；
3. 输出 "Breakpoint!" / "Illegal instruction!" → grade.sh sentinel 匹配不到；应改成 "... at 0x..."；
4. default 推进 epc → 未修复的异常被跳过；应只 print_trapframe；
5. %08x 打 64 位 → 只读 32 位；应 %016lx；%016lx 必须对应 64 位参数；
6. 改动其他函数/trapframe → 违反实验约束；应只保留 insn_len + exception_handler。
````

---

## 4. 这三份提示词分别解决什么问题

| 提示词 | 解决的问题 | 对应最终代码/测试 |
|---|---|---|
| 主实现 | 让 AI 生成正确的 `insn_len` + `exception_handler` | `kern/trap/trap.c` |
| 测试生成 | 证明 `+4`、`+2`、兜底三条路径 | 6.1～6.4 测试 |
| 代码审查 | 抓固定 `+4`、`static`、输出关键字、默认分支等坑 | 迭代记录中的 bug 表 |

---

## 5. 拆分提示词时的一个原则

不要把“实现”和“验证”混在一个提示词里。

- 主实现提示词只负责“写对”；
- 测试提示词只负责“证明它对”；
- 审查提示词只负责“找它哪里可能不对”。

三份分开，AI 不容易在一个提示词里顾此失彼；你自己也更容易定位问题出在哪一层。
