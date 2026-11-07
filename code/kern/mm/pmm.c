/* =============================================================================
 * pmm.c —— 物理内存管理（PMM）框架层（lab2 实现，lab3 沿用）
 *
 * 【本文件的角色】它不实现具体分配算法，而是"框架 + 分发器"：
 *   - 真正的算法在 kern/mm/best_fit_pmm.c（best_fit 最佳适配），通过
 *     pmm_manager 函数指针表挂接进来（见 init_pmm_manager）；
 *   - 本文件负责三件事：
 *     1) page_init     ：探测物理内存、划掉保留区、把空闲页注册给管理器
 *     2) alloc/free 等 ：关中断临界区 + 转发给 pmm_manager
 *     3) pmm_init      ：启动时按顺序把以上流程拉起来
 *
 * 【物理页怎么被"管理"】用一个 struct Page 描述一个 4KB 物理页，所有描述符
 *   排成数组 pages[]，下标 = 物理地址对应页号 - nbase。从此"管理物理内存"
 *   变成"管理这个数组"，算法只需操作数组元素（见 memlayout.h/pmm.h）。
 * ========================================================================== */

#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <../sync/sync.h>
#include <riscv.h>
#include <dtb.h>

// ---------------------------------------------------------------------------
// 全局状态（其他文件经 pmm.h 的 extern 声明引用）
// ---------------------------------------------------------------------------

// pages ：物理页描述符数组的虚拟地址。数组第 i 项描述物理页号 (nbase+i)。
//         这是 pmm 的核心数据结构入口——分配/释放算法操作的就是数组元素。
struct Page *pages;
// npage ：物理内存末尾的绝对页号（exclusive，不是"页数"）。
//         可管理的页数 = npage - nbase；npage 在 page_init 里由 DTB 信息算出。
size_t npage = 0;
// va_pa_offset ：内核虚拟地址与物理地址的固定偏移 = 0xFFFFFFFF40000000。
//         内核被链接在 VA=0xFFFFFFFFC0000000 处，实际加载在 PA=0x80000000，
//         任何 VA↔PA 转换（KADDR/PADDR 宏）都依赖它。
//         含义：内核镜像在 VA=KERNBASE 处运行，对应物理地址 info.base。
uint64_t va_pa_offset;
// nbase ：第一个"可管理"物理页的页号。RISC-V 的 DRAM 从 0x80000000 开始
//         （DRAM_BASE，定义在 riscv.h），之前的地址空间不是 RAM，
//         所以 nbase = 0x80000000 / 4096 = 0x20000，这之前的页号一律不管。
const size_t nbase = DRAM_BASE / PGSIZE;

// satp_virtual / satp_physical ：entry.S 里那张启动页表（boot_page_table_sv39）
//   的虚拟/物理地址，pmm_init 末尾记录下来，供后续页表相关代码取用。
uintptr_t *satp_virtual = NULL;
uintptr_t satp_physical;

// pmm_manager ：当前选用的分配算法（函数指针表，struct 定义见 pmm.h）。
//   本内核选用 best_fit（最佳适配），在 init_pmm_manager 里赋值。
const struct pmm_manager *pmm_manager;

/* lab3 的 memlayout.h 只保留了 PHYSICAL_MEMORY_OFFSET，删掉了 lab2 里的
 * PHYSICAL_MEMORY_END。这里用 KERNTOP 换算出物理地址上界：
 * 0xFFFFFFFFC8000000 - 0xFFFFFFFF40000000 = 0x88000000。
 * 保持 lab2 的 page_init 逻辑不变，只在本地补一个宏。 */
#define PHYSICAL_MEMORY_END (KERNTOP - PHYSICAL_MEMORY_OFFSET)


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// ---------------------------------------------------------------------------
// 三个对外接口：alloc_pages / free_pages / nr_free_pages
// 三者是同一套模板 —— 关中断临界区 + 转发给 pmm_manager：
//   local_intr_save/restore 来自 kern/sync/sync.h，进入前保存 sstatus.SIE
//   并关中断（flag=1），退出时按 flag 恢复原状（嵌套安全）。
//   为什么必须关中断：空闲页链表是全局共享数据，"查链表 + 摘链/插链"如果
//   被时钟中断打断、中断里又触发分配，链表会被并发改坏（丢页/成环）。
//   lab3 单 hart 无 SMP 竞争，这里的保护对象就是中断嵌套这一种情形。
// ---------------------------------------------------------------------------

// alloc_pages - 分配 n 个连续物理页，返回首页描述符指针（失败返回 NULL）
// 实际的适配查找在 best_fit_pmm.c 的 best_fit_alloc_pages 里完成
struct Page *alloc_pages(size_t n) {
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - 归还从 base 开始的 n 个连续物理页（重新挂回空闲链表）
// 实现在 best_fit_pmm.c 的 best_fit_free_pages
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - 查询当前空闲页总数（单位：页，乘 PGSIZE 才是字节数）
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
//lab2 function1 2510538

/* page_init - detect physical memory space, reserve already used memory,
 * then use pmm->init_memmap to create free page list.
 * 根据设备树提供的物理内存信息，建立可管理物理页的描述符布局，
 * 把排除 OpenSBI、内核镜像和描述符数组占用后的完整空闲页面注册给当前 pmm_manager。 */
static void page_init(void) {
    // 1. 虚实偏移量 = 内核虚拟基址 - DRAM 物理基址
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // 2. 从设备树读物理内存范围（dtb_init 已解析并存入全局变量）
    uint64_t mem_begin = get_memory_base();
    uint64_t mem_size  = get_memory_size();
    if (mem_begin == 0 || mem_size == 0) {
        panic("page_init: DTB memory info not available");
    }
    uint64_t maxpa = mem_begin + mem_size;   // 物理内存末尾（exclusive）
    if (maxpa > PHYSICAL_MEMORY_END) {
        maxpa = PHYSICAL_MEMORY_END;
    }
    cprintf("physcial memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            mem_size, mem_begin, maxpa - 1);

    // 3. npage = 物理内存末尾的绝对页号（exclusive），不是"页数"
    npage = maxpa >> PGSHIFT;

    // 4. 描述符数组起点紧接内核结束地址，按页向上对齐
    extern char end[];
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    // 5. 先把所有描述符置为保留，防止未完成布局的页面被提前分配
    for (size_t i = 0; i < npage - nbase; i++) {
        SetPageReserved(pages + i);
    }

    // 6. 空闲内存起点 = 描述符数组末尾转回物理地址后按页向上对齐
    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));
    freemem = ROUNDUP(freemem, PGSIZE);

    // 7. 空闲终点按页向下对齐；两者之间有完整空闲页面时才注册
    uint64_t free_end = ROUNDDOWN(maxpa, PGSIZE);
    if (freemem < free_end) {
        init_memmap(pa2page(freemem), (free_end - freemem) >> PGSHIFT);
    }
}

/* pmm_init - 物理内存管理总入口（init.c 在内核启动最早期调用）
 * 四步固定流程：
 *   1) init_pmm_manager ：选定算法（best_fit）并初始化其空闲链表
 *   2) page_init        ：从 DTB 拿物理内存范围，划掉保留区，注册空闲页
 *   3) check_alloc_page ：跑 best_fit_pmm.c 里的自检函数，验证分配/释放正确
 *   4) 记录启动页表地址 ：satp_virtual/physical，供后续页表代码使用
 * 自检通过的标志就是启动日志里的 "check_alloc_page() succeeded!" */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
