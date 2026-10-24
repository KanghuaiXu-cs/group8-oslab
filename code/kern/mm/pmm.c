#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <slub_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    // 任务二：Best-Fit；任务三：Buddy System。
    // 两者都实现了 struct pmm_manager 的七个成员，切换只改这一行的指向。
    pmm_manager = &buddy_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

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

/* pmm_init - initialize the physical memory management */
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

    // 扩展 Challenge：任意大小内存对象（slub）分配的自测
    slub_test();
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
