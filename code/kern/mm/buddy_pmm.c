#include <pmm.h>
#include <list.h>
#include <buddy_pmm.h>
#include <stdio.h>

// lab2 任务三：Buddy System（伙伴系统）连续物理页分配
// 2510538 张铭瑞
//
// 设计要点：
//   1. 空闲空间按"阶"组织：阶 k 的块固定含 2^k 页，所以块大小天然是 2 的幂；
//   2. 每个阶一条自由链表，块头 Page 的 property 记录该块页数；
//   3. 分配：把需求页数向上取整到 2^k，从阶 k 开始往上找第一个非空阶，取出一块后
//      反复对半劈，多出来的后半块留在低阶，直到剩下的正好是 2^k；
//   4. 释放：把块归位到阶 k，然后看"伙伴"是否也空闲且同阶，是则合并成阶 k+1，
//      继续往上，直到不能再合并。
//
// 伙伴为什么用异或算：同一阶的两块若来自同一次劈分，页号只有第 k 位不同，
// 页号异或 2^k 正好互为对方。

#define MAX_ORDER        11                          // 合法阶 0..10
#define ORDER_MAX_PAGES  (1UL << (MAX_ORDER - 1))    // 单块最大 2^10 = 1024 页 = 4MB

static free_area_t free_area[MAX_ORDER];
static size_t nr_free_total;

#define heads(i)  (free_area[(i)].free_list)

/* 把一个从 base 起、大小为 2^order 页的空闲块挂到第 order 阶链表 */
static void
buddy_push(struct Page *base, int order) {
    base->property = (unsigned int)(1UL << order);
    SetPageProperty(base);
    list_add(&heads(order), &(base->page_link));
}

/* page 是否正挂在第 order 阶链表上 —— 判断伙伴是否空闲 */
static int
buddy_in_order(struct Page *page, int order) {
    list_entry_t *le = &heads(order);
    while ((le = list_next(le)) != &heads(order)) {
        if (le2page(le, page_link) == page) {
            return 1;
        }
    }
    return 0;
}

static void
buddy_init(void) {
    for (int i = 0; i < MAX_ORDER; i++) {
        list_init(&heads(i));
        free_area[i].nr_free = 0;
    }
    nr_free_total = 0;
}

/* 把 [base, base+n) 这 n 个连续页交给伙伴系统 */
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    // 从左往右切：每一刀的块大小必须是 2 的幂，且起点按该大小对齐。
    // 不对齐就凑不出真正的伙伴，释放时永远合并不回去。
    struct Page *cur = base;
    size_t remain = n;
    while (remain > 0) {
        ppn_t ppn = page2ppn(cur);
        int order = 0;
        while (order + 1 < MAX_ORDER &&
               (1UL << (order + 1)) <= remain &&
               (ppn & ((1UL << (order + 1)) - 1)) == 0) {
            order++;
        }
        buddy_push(cur, order);
        free_area[order].nr_free += (unsigned int)(1UL << order);
        nr_free_total += (1UL << order);
        cur += (1UL << order);
        remain -= (1UL << order);
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free_total) {
        return NULL;
    }

    // 1. 需求页数向上取整到 2 的幂
    int need = 0;
    while ((1UL << need) < n) {
        need++;
    }
    if (need >= MAX_ORDER) {
        return NULL;                       // 超过单块上限，无法满足
    }

    // 2. 从 need 阶往上找第一个非空阶
    int order = need;
    while (order < MAX_ORDER && list_empty(&heads(order))) {
        order++;
    }
    if (order >= MAX_ORDER) {
        return NULL;
    }

    // 3. 摘下这块
    list_entry_t *le = list_next(&heads(order));
    struct Page *page = le2page(le, page_link);
    list_del(&(page->page_link));
    ClearPageProperty(page);
    free_area[order].nr_free -= (unsigned int)(1UL << order);

    // 4. 逐阶对半劈，后半块留在低阶
    while (order > need) {
        order--;
        struct Page *half = page + (1UL << order);
        buddy_push(half, order);
        free_area[order].nr_free += (unsigned int)(1UL << order);
    }

    nr_free_total -= (1UL << need);
    page->property = (unsigned int)(1UL << need);
    return page;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    int order = 0;
    while ((1UL << order) < n) {
        order++;
    }
    assert(order < MAX_ORDER);

    // 1. 清掉块内所有页的分配痕迹
    struct Page *p = base;
    for (; p != base + (1UL << order); p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    nr_free_total += (1UL << order);

    // 2. 逐阶向上合并：伙伴同阶且空闲才能合
    while (order < MAX_ORDER - 1) {
        ppn_t bppn = page2ppn(base) ^ (1UL << order);
        if (bppn < nbase || bppn >= npage) {
            break;
        }
        struct Page *buddy = pages + (bppn - nbase);
        if (!buddy_in_order(buddy, order)) {
            break;
        }
        list_del(&(buddy->page_link));
        ClearPageProperty(buddy);
        free_area[order].nr_free -= (unsigned int)(1UL << order);
        if (buddy < base) {
            base = buddy;                  // 合并后以低地址那块为新的块头
        }
        order++;
    }

    // 3. 挂到最终的阶
    buddy_push(base, order);
    free_area[order].nr_free += (unsigned int)(1UL << order);
}

static size_t
buddy_nr_free_pages(void) {
    return nr_free_total;
}

static void
buddy_check(void) {
    size_t nr0 = nr_free_total;
    struct Page *p[8];

    // 1. 各阶单独分配：检查 2^k 页对齐、块大小、以及"已分配"标志
    for (int k = 0; k < 8; k++) {
        p[k] = alloc_pages(1UL << k);
        assert(p[k] != NULL);
        assert((page2ppn(p[k]) & ((1UL << k) - 1)) == 0);
        assert(p[k]->property == (unsigned int)(1UL << k));
        assert(!PageProperty(p[k]));
    }
    // 2. 逆序全部释放，空闲页数必须精确回到原值
    for (int k = 7; k >= 0; k--) {
        free_pages(p[k], 1UL << k);
    }
    assert(nr_free_total == nr0);

    // 3. 非 2 幂页数：申请 3 页应取整到阶 2（4 页）
    struct Page *q = alloc_pages(3);
    assert(q != NULL);
    assert(q->property == 4);
    assert((page2ppn(q) & 3) == 0);
    assert(nr_free_total == nr0 - 4);
    free_pages(q, 3);
    assert(nr_free_total == nr0);

    // 4. 拆分：连续申请小块，迫使高阶块被逐层劈开
    struct Page *r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = alloc_pages(1);
        assert(r[i] != NULL);
    }
    assert(nr_free_total == nr0 - 4);
    for (int i = 0; i < 4; i++) {
        free_pages(r[i], 1);
    }
    assert(nr_free_total == nr0);

    // 5. 边界：请求超过单块上限，必须返回 NULL 且不改变空闲数
    assert(alloc_pages(ORDER_MAX_PAGES * 2) == NULL);
    assert(nr_free_total == nr0);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
