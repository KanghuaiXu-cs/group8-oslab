#ifndef __KERN_MM_SLUB_PMM_H__
#define __KERN_MM_SLUB_PMM_H__

#include <defs.h>

/* 第二层：任意大小内存对象分配 */
void *slub_alloc(size_t size);
void slub_free(void *ptr);

/* 自测入口：在 pmm_init() 末尾调用可验证实现是否正确 */
void slub_test(void);

#endif /* !__KERN_MM_SLUB_PMM_H__ */
