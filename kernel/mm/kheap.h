/* kernel/mm/kheap.h */
#ifndef ZENEDGE_KHEAP_H
#define ZENEDGE_KHEAP_H

#include <stddef.h>
#include <stdint.h>

/* Initialize Kernel Heap */
void kheap_init(void *start_addr, size_t size);

/* Standard Allocators */
void *kmalloc(size_t size);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);

/* Stats */
size_t kheap_free_size(void);

#endif
