# Lab2 提示词汇总

> 本文件汇总本实验用到的全部 prompt，与 report.md 第四节中的提示词一一对应。
> 全部采用四段式结构（[PROMPT] / [RELY] / [GUARANTEE] / [SPECIFICATION]）。

## 功能模块：物理内存初始化功能

### 提示词 1：最终提示词

```markdown
[PROMPT]
在 kern/mm/pmm.c 中实现 page_init()。任务：根据设备树提供的物理内存信息，建立可管理物理页的描述符布局，把排除 OpenSBI、内核镜像和描述符数组占用空间后的完整空闲页面注册给当前物理内存管理器（pmm_manager），为后续页面分配提供正确的初始状态。
操作约束：必须在项目中修改实际文件（kern/mm/pmm.c），保留原有代码，只在标记位置补全；不要引入与本任务无关的功能。
输出要求：使用下方 [RELY]、[GUARANTEE]、[SPECIFICATION] 中提供的信息，以工程真实代码为准。

[RELY]
- 已完成的探测：dtb_init() 已把设备树里的物理内存起点与大小解析并保存到全局变量 memory_base、memory_size（kern/driver/dtb.c）
- 框架提供的函数与宏：get_memory_base()、get_memory_size()、panic()、cprintf()、init_memmap()、pa2page()、set_page_ref()、SetPageReserved()、ClearPageReserved()、PageReserved()、SetPageProperty()、PPN()、PADDR()、KADDR()、ROUNDUP()、PGSIZE、PGSHIFT、DRAM_BASE、KERNBASE
- 全局变量与链接符号：va_pa_offset、npage、pages、nbase，链接符号 end（内核结束的虚拟地址）
- 数据结构：struct Page（kern/mm/memlayout.h）、struct pmm_manager（kern/mm/pmm.h）
- 平台事实：QEMU virt，DRAM 起始物理地址 0x80000000，默认 128MB（即 [0x80000000, 0x88000000)）；OpenSBI 占用 [0x80000000, 0x80200000)；内核镜像占 [0x80200000, KernelEnd)；虚实偏移量 va_pa_offset = 0xFFFFFFFF40000000

[GUARANTEE]
- 交付函数：static void page_init(void)（kern/mm/pmm.c）
- 成功返回时应正确设置三个全局变量：va_pa_offset、npage、pages
- 输出探测到的物理内存范围（调用 cprintf 打印）
- 完成当前物理内存管理器初始空闲区的注册（调用 init_memmap 把空闲区间交出去）

[SPECIFICATION]
前置条件：dtb_init() 已完成；memory_base 与 memory_size 已有效。
行为规格：
1. va_pa_offset = 0xFFFFFFFFC0000000 - 0x80000000（内核虚拟基址与 DRAM 物理基址之差）。
2. npage = PPN(mem_end)，其中 mem_end = memory_base + memory_size - 1。注意：npage 是"物理内存末尾的绝对页号"，不是"页数"；描述符数组的元素个数为 npage - nbase。
3. pages = ROUNDUP((uintptr_t)end, PGSIZE)：描述符数组起点紧接内核结束地址，按页向上对齐。
4. 把描述符数组内每个 Page 先全部置为保留（SetPageReserved），防止未完成布局的页面被提前分配。
5. 空闲起点 freemem = ROUNDUP(PADDR(pages + (npage - nbase)), PGSIZE)：把描述符数组末尾转回物理地址后按页向上对齐。
6. 空闲终点 mem_end 按页向下对齐（去掉末尾不足一页的部分）。
7. 仅当 freemem < mem_end（两者之间存在完整空闲页面区间）时，才调用 init_memmap(pa2page(freemem), (mem_end - freemem) >> PGSHIFT)。
8. 内核镜像与描述符数组所在的页面不得进入空闲区。
失败判据：
- 若 memory_base / memory_size 无效（设备树信息无效），应调用 panic() 终止初始化，不得继续。
- 若 freemem >= mem_end（没有可用的完整空闲页面），不得调用 init_memmap()。
边界情况：页面对齐；末尾不足一页的尾巴不交给分配器。
```

## 功能模块：Best-Fit 连续物理内存分配

### 提示词 2：最终提示词

```markdown
[PROMPT]
在 kern/mm/best_fit_pmm.c 中实现完整的 Best-Fit 连续物理内存分配模块，并完成注册。该文件已提供 basic_check() 与 best_fit_check() 两个测试函数，不提供具体管理函数的声明和 best_fit_pmm_manager 注册结构——由你根据 pmm.h 中 struct pmm_manager 的成员职责自行设计内部状态和管理函数。
操作约束：必须在项目中修改实际文件；不得修改两个测试函数；不要改动 pmm.h、memlayout.h、list.h 等已有接口与数据结构。
输出要求：生成的实现必须符合测试函数使用的状态约定，能通过 best_fit_check 与 basic_check。

[RELY]
- struct pmm_manager 的完整定义（kern/mm/pmm.h）：七成员（name / init / init_memmap / alloc_pages / free_pages / nr_free_pages / check），除 name 外均为函数指针
- First-Fit 的完整参照实现：kern/mm/default_pmm.c 中的 default_init / default_init_memmap / default_alloc_pages / default_free_pages / default_nr_free_pages / default_check
- 数据结构：struct Page（ref / flags / property / page_link，kern/mm/memlayout.h）、free_area_t（free_list + nr_free）
- 链表与宏：libs/list.h 的双向链表 API（list_init / list_add / list_add_before / list_next / list_prev / list_empty / list_del），le2page 宏，SetPageProperty / ClearPageProperty / PageProperty / set_page_ref / PageReserved
- 两个测试函数的行为约定：basic_check 做通用的分配/释放/合并检查；best_fit_check 额外要求"选块必须是最小匹配"

[GUARANTEE]
- 交付：在 kern/mm/best_fit_pmm.c 中实现完整的 Best-Fit 模块，并定义 const struct pmm_manager best_fit_pmm_manager 完成注册（七个成员全部赋值，其中 check 指向 best_fit_check）
- 必须实现的管理函数（内部函数，非对外接口）：best_fit_init、best_fit_init_memmap、best_fit_alloc_pages、best_fit_free_pages、best_fit_nr_free_pages
- 内部辅助函数可添加，但不得作为对外接口

[SPECIFICATION]
分配（best_fit_alloc_pages(size_t n)）：
- 前置：assert(n > 0)
- 失败：n > nr_free 时返回 NULL，且不得修改原空闲链表与 nr_free
- 选块：遍历空闲链表，在所有 property >= n 的块中取 property 最小者（最小匹配）
- 成功：从链表摘除该块；若 property > n，剩余部分设为 property = 原值 - n、SetPageProperty、按地址顺序插回链表；nr_free -= n；对被分配块执行 ClearPageProperty
释放（best_fit_free_pages(struct Page *base, size_t n)）：
- 前置：assert(n > 0)；对 base..base+n 每个页 assert(!PageReserved(p) && !PageProperty(p))
- 逐页清 flags、set_page_ref(p, 0)；设 base->property = n、SetPageProperty(base)；nr_free += n
- 按地址有序插入空闲链表
- 先尝试向前合并：若前驱块的末尾恰好衔接 base（p + p->property == base），则合并
- 再尝试向后合并：若 base 末尾恰好衔接后继块（base + base->property == p），则合并
通用约束：切割出的空闲块必须按地址顺序插入空闲链表；失败路径不得修改原链表与计数。
```

## 练习：Buddy System（伙伴系统）分配算法

### 提示词 3：第一份提示词（生成实现）

```markdown
[PROMPT]
参考现有 ucore 物理内存管理框架（pmm.h 的 struct pmm_manager、default_pmm.c 的接入方式），从零实现 Buddy System（伙伴系统）连续物理页分配模块，文件为 kern/mm/buddy_pmm.c，并定义 buddy_pmm_manager 完成注册、接入内核。
操作约束：必须在项目中修改实际文件；不得修改 pmm.h、memlayout.h、list.h 等已有接口；不得改动 default_pmm.c 与 best_fit_pmm.c。
输出要求：生成的模块必须能正常编译、能接入 pmm_init() 的调用链。

[RELY]
- struct pmm_manager（kern/mm/pmm.h，七成员函数指针）
- struct Page（ref / flags / property / page_link）、free_area_t、双向链表 API（libs/list.h）、le2page 宏
- 接入方式参照：default_pmm.c 的 default_pmm_manager 与 pmm.c 的 init_pmm_manager()
- 伙伴计算规则：同阶两块互为伙伴，伙伴页号 = page2ppn(base) XOR size（size 为该阶块大小）

[GUARANTEE]
- 交付 kern/mm/buddy_pmm.c，含完整的 buddy_pmm_manager（七成员全部赋值）
- 内部实现函数：buddy_init、buddy_init_memmap、buddy_alloc_pages、buddy_free_pages、buddy_nr_free_pages、buddy_check（check 用你自行设计的测试，见第二份提示词）
- 算法的时空复杂度不做要求，能通过自写测试即可

[SPECIFICATION]
- 常量：MAX_ORDER = 11，合法阶 k ∈ [0, 10]，块大小 = 2^k 页，最大块 1024 页（4MB）
- 数据结构：按阶组织空闲链表（每个阶一条 free_list）
- 分配 alloc_pages(n)：将 n 向上取整到 2^k 得到阶 k；在阶 k 的空闲链表中找块，找不到则沿更高阶往下切（每次一分为二，直到得到阶 k 的块）
- 释放 free_pages(base, n)：先归位到阶 k，然后用 page2ppn(base) XOR size 找伙伴；若伙伴空闲且同阶则合并为更高阶，重复向上直到不能合并
- 成功/失败与边界：n = 0 按断言错误处理；n 超过最大可分配块时返回 NULL
```

### 提示词 4：第二份提示词（生成测试）

```markdown
[PROMPT]
针对第一份提示词生成的 buddy_pmm.c（Buddy System 分配模块）设计测试函数 buddy_check，验证其实现的正确性。
操作约束：测试函数写在 kern/mm/buddy_pmm.c 中；不得修改被测试的实现代码；测试必须能编译运行并通过。

[RELY]
- buddy_pmm.c 的完整实现（第一份提示词的产出）
- 已有测试函数参照：best_fit_pmm.c 中的 basic_check 与 best_fit_check

[GUARANTEE]
- 交付 buddy_check 函数，覆盖核心行为与边界情况
- 测试失败时应能定位到具体场景（打印失败原因）

[SPECIFICATION]
测试需覆盖以下场景：
- 基本分配与释放：分配若干页、释放、检查空闲页数回到初始值
- 阶取整：分配非 2 幂页数（如 3 页），应向上取整到阶 2（4 页）
- 拆分：空闲区不足时沿更高阶向下切块
- 合并：释放后相邻同阶伙伴应合并为更大块
- 边界：n = 0 触发断言；n 超过最大阶时返回 NULL
- 压力：多次分配释放后空闲链表不出现泄漏或重叠
```

## Challenge：任意大小的内存单元 slub 分配算法（选做）

### 提示词 5：第一份提示词（生成实现）

```markdown
[PROMPT]
在没有现成代码框架的情况下，从零生成 SLUB 内存对象分配模块并接入现有内核（kern/mm/slub_pmm.c）。SLUB 是两层架构：第一层基于页大小的内存分配（复用现有物理页分配器），第二层在第一层基础上实现基于任意大小的内存分配。可简化实现，能体现其主体思想即可。

[RELY]
- 现有物理页分配器：pmm_manager（default_pmm_manager / best_fit_pmm_manager）
- struct Page、list.h、内核现有基础设施

[GUARANTEE]
- 交付 kern/mm/slub_pmm.c，含 slub_pmm_manager 注册
- 实现任意大小内存对象的分配与释放接口

[SPECIFICATION]
- 第一层：向底层页分配器申请整页
- 第二层：在页内按对象大小划分 slot，用空闲链表管理
- 分配：优先从现有 slab 的空闲 slot 中取；没有则向第一层申请新页建立新 slab
- 释放：slot 归还给所属 slab；slab 全空闲时可释放整页回第一层
- 边界：分配 0 大小、释放未分配指针等异常场景的处理
```

### 提示词 6：第二份提示词（生成测试）

```markdown
[PROMPT]
针对第一份提示词生成的 slub_pmm.c 设计测试函数，检验不同大小内存对象的分配和释放是否正确。

[RELY]
- slub_pmm.c 的完整实现

[GUARANTEE]
- 交付测试函数，覆盖核心行为与边界情况

[SPECIFICATION]
- 分配不同大小对象（小、中、大），释放后无泄漏
- 边界：分配 0 大小、重复释放、释放越界指针
- 压力：大量分配释放循环后系统状态一致
```
