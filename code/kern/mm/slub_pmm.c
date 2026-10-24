#include <pmm.h>
#include <list.h>
#include <slub_pmm.h>
#include <stdio.h>
#include <string.h>

// lab2 扩展 Challenge：任意大小内存单元 slub 分配算法
// 2510538 张铭瑞
//
// 两层架构：
//   第一层 —— 页级分配：直接向 pmm_manager 要整页（alloc_pages(1)）；
//   第二层 —— 对象级分配：在一页内部按固定尺寸切成若干等长 slot，
//             同一页的 slot 用页内单链表串起来（空闲对象的前 8 字节存下一个空闲对象的地址）。
//
// 分配任意大小对象时，先向上取整到最近的尺寸档（8/16/32/64/128/256/512/1024），
// 再从该档的 slab 里取一个 slot；档内没有空位就向第一层要一整页、建成新 slab。
//
// 简化说明：空 slab 不立即归还给第一层，而是留在档内等待复用 —— 这正是 SLUB
// 相对 SLAB 的取舍之一（省掉频繁归还带来的抖动），也避免本实现引入额外的
// 链表摘除逻辑。整页归还留作可扩展点。

#define SLUB_NCLASS   8
#define SLUB_HDR      64                              // 每页头部留给 slub_page 的空间

static const size_t slub_sizes[SLUB_NCLASS] = {8, 16, 32, 64, 128, 256, 512, 1024};

/* 放在每个物理页开头的 slab 元信息 */
struct slub_page {
    struct slub_page *next;      // 同一尺寸档的下一个 slab
    void *freelist;              // 本页内空闲对象链
    unsigned int obj_size;       // 单个对象大小
    unsigned int inuse;          // 本页已分配对象数
    unsigned int total;          // 本页能容纳的对象数
    struct Page *page;           // 底层物理页描述符
};

static struct slub_page *slub_classes[SLUB_NCLASS];

/* 物理页描述符 -> 内核虚拟地址（pmm.h 里的 KADDR 被注释掉了，这里自己算） */
static inline void *page2kva(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

/* 尺寸向上取整到档位；超过最大档返回 -1 */
static int slub_class_of(size_t size) {
    for (int i = 0; i < SLUB_NCLASS; i++) {
        if (size <= slub_sizes[i]) {
            return i;
        }
    }
    return -1;
}

/* 向第一层要一整页，建成新 slab，把页内对象串成空闲链 */
static struct slub_page *slub_grow(int cls) {
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }
    struct slub_page *sp = (struct slub_page *)page2kva(page);
    sp->obj_size = (unsigned int)slub_sizes[cls];
    sp->total = (unsigned int)((PGSIZE - SLUB_HDR) / sp->obj_size);
    sp->inuse = 0;
    sp->freelist = NULL;
    sp->page = page;

    char *base = (char *)sp + SLUB_HDR;
    for (unsigned int i = 0; i < sp->total; i++) {
        void *obj = base + i * sp->obj_size;
        *(void **)obj = sp->freelist;
        sp->freelist = obj;
    }

    sp->next = slub_classes[cls];
    slub_classes[cls] = sp;
    return sp;
}

void *
slub_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    int cls = slub_class_of(size);
    if (cls < 0) {
        return NULL;                       // 超过单页能承载的最大档
    }

    struct slub_page *sp = slub_classes[cls];
    while (sp != NULL && sp->inuse >= sp->total) {
        sp = sp->next;
    }
    if (sp == NULL) {
        sp = slub_grow(cls);
        if (sp == NULL) {
            return NULL;                   // 第一层也没页了
        }
    }

    void *obj = sp->freelist;
    if (obj == NULL) {
        return NULL;
    }
    sp->freelist = *(void **)obj;
    sp->inuse++;
    memset(obj, 0, sp->obj_size);
    return obj;
}

void
slub_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    // 对象一定落在某个 slab 页内，页首就是它的 slub_page
    struct slub_page *sp = (struct slub_page *)((uintptr_t)ptr & ~(uintptr_t)(PGSIZE - 1));
    if (sp->page == NULL || sp->obj_size == 0 || sp->inuse == 0) {
        return;                            // 不是本分配器发出的指针，直接忽略
    }
    *(void **)ptr = sp->freelist;
    sp->freelist = ptr;
    sp->inuse--;
}

void
slub_test(void) {
    cprintf("slub_test: begin\n");

    // 1. 三种尺寸档各取一个对象，写入后读回，验证三档互不干扰
    void *a = slub_alloc(8);
    void *b = slub_alloc(64);
    void *c = slub_alloc(1000);
    assert(a != NULL && b != NULL && c != NULL);
    assert(a != b && b != c && a != c);

    memset(a, 'A', 8);
    memset(b, 'B', 64);
    memset(c, 'C', 1000);
    for (int i = 0; i < 8; i++)   assert(((char *)a)[i] == 'A');
    for (int i = 0; i < 64; i++)  assert(((char *)b)[i] == 'B');
    for (int i = 0; i < 1000; i++) assert(((char *)c)[i] == 'C');

    slub_free(a);
    slub_free(b);
    slub_free(c);

    // 2. 释放后重新分配，应能复用刚归还的对象
    void *d = slub_alloc(8);
    assert(d != NULL);
    memset(d, 'D', 8);
    for (int i = 0; i < 8; i++) assert(((char *)d)[i] == 'D');
    slub_free(d);

    // 3. 边界：0 大小、超过最大档，必须返回 NULL
    assert(slub_alloc(0) == NULL);
    assert(slub_alloc(4096) == NULL);
    // 4. 边界：释放空指针不得崩溃
    slub_free(NULL);

    cprintf("slub_test: passed\n");
}
