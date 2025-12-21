/* kernel/mm/kheap.c */
#include <stdint.h>
#include <stddef.h>

#include "kheap.h"
#include "../console.h"
#include "../include/string.h"

#define HEAP_MAGIC 0xC0FFEE42u
#define ALIGN_UP(x, a)   (((x) + ((a)-1)) & ~((a)-1))
#define ALIGN_DOWN(x, a) ((x) & ~((a)-1))

/*
 * Ensure 16-byte payload alignment on both i386 and x86_64.
 * The header size itself must be a multiple of 16 so that:
 * aligned_base + sizeof(header) == aligned_payload
 */
typedef struct block_header {
    uint32_t magic;
    uint32_t is_free;
    size_t   size;              // payload size (bytes)
    struct block_header* next;
    struct block_header* prev;
#if defined(__x86_64__)
    uint64_t _pad0;   // x64: 4+4+8+8+8 = 32. +8 = 40. Aligned to 16 -> 48 bytes.
#else
    uint32_t _pad0;
    uint32_t _pad1;   // i386: 4+4+4+4+4 = 20. +4+4 = 28. Aligned to 16 -> 32 bytes.
#endif
} __attribute__((aligned(16))) block_header_t;

static block_header_t* heap_start = NULL;
static uintptr_t heap_base = 0;
static uintptr_t heap_end  = 0;

static int ptr_in_heap(void* p) {
    uintptr_t u = (uintptr_t)p;
    return (u >= heap_base) && (u < heap_end);
}

void kheap_init(void* start_addr, size_t size) {
    if (!start_addr) return;

    // Align heap start to 16
    uintptr_t base = ALIGN_UP((uintptr_t)start_addr, 16);
    uintptr_t end  = (uintptr_t)start_addr + (uintptr_t)size;
    end = ALIGN_DOWN(end, 16);

    if (end <= base + sizeof(block_header_t) + 16) return;

    heap_base = base;
    heap_end  = end;

    heap_start = (block_header_t*)base;
    heap_start->magic = HEAP_MAGIC;
    heap_start->size  = (size_t)(end - base - sizeof(block_header_t));
    heap_start->next  = NULL;
    heap_start->prev  = NULL;
    heap_start->is_free = 1;

    console_write("[kheap] initialized (aligned, 64-bit safe)\n");
}

static void split_block(block_header_t* b, size_t want) {
    size_t remaining = b->size - want;
    if (remaining <= (size_t)(sizeof(block_header_t) + 16)) return;

    // Because 'b' is 16-aligned and sizeof(block_header_t) is 16-aligned,
    // nb_addr is naturally 16-aligned. No explicit ALIGN_UP needed.
    uintptr_t nb_addr = (uintptr_t)b + sizeof(block_header_t) + want;
    
    block_header_t* nb = (block_header_t*)nb_addr;
    nb->magic = HEAP_MAGIC;
    nb->is_free = 1;

    uintptr_t b_payload_end = (uintptr_t)b + sizeof(block_header_t) + b->size;
    size_t new_size = (size_t)(b_payload_end - (uintptr_t)nb - sizeof(block_header_t));

    nb->size = new_size;
    nb->next = b->next;
    nb->prev = b;

    if (b->next) b->next->prev = nb;

    b->next = nb;
    b->size = want;
}

void* kmalloc(size_t size) {
    if (!heap_start || size == 0) return NULL;

    size_t want = ALIGN_UP(size, 16);

    block_header_t* cur = heap_start;
    while (cur) {
        if (cur->magic != HEAP_MAGIC) {
            console_write("[kheap] CORRUPTION (bad magic)\n");
            return NULL;
        }

        if (cur->is_free && cur->size >= want) {
            split_block(cur, want);
            cur->is_free = 0;
            return (void*)((uintptr_t)cur + sizeof(block_header_t));
        }
        cur = cur->next;
    }

    console_write("[kheap] OOM\n");
    return NULL;
}

static void coalesce(block_header_t* b) {
    if (!b) return;
    if (!ptr_in_heap(b) || b->magic != HEAP_MAGIC) return;

    /* If this is a free block, first walk backward to the start of the free run. */
    if (b->is_free) {
        while (b->prev && b->prev->is_free && b->prev->magic == HEAP_MAGIC) {
            b = b->prev;
        }
    }

    /* Merge forward repeatedly */
    while (b->next && b->next->is_free && b->next->magic == HEAP_MAGIC) {
        block_header_t* n = b->next;

        uintptr_t payload_start = (uintptr_t)b + sizeof(block_header_t);
        uintptr_t next_end = (uintptr_t)n + sizeof(block_header_t) + (uintptr_t)n->size;

        if (next_end <= payload_start || next_end > heap_end) {
            console_write("[kheap] CORRUPTION (bad coalesce)\n");
            break;
        }

        size_t merged_payload = (size_t)(next_end - payload_start);
        b->size = merged_payload;
        b->next = n->next;
        if (b->next) b->next->prev = b;
    }
}

void kfree(void* ptr) {
    if (!ptr) return;
    if (!ptr_in_heap(ptr)) return;

    block_header_t* h = (block_header_t*)((uintptr_t)ptr - sizeof(block_header_t));
    if (!ptr_in_heap(h) || h->magic != HEAP_MAGIC) {
        console_write("[kheap] kfree invalid ptr\n");
        return;
    }

    if (h->is_free) {
        console_write("[kheap] double free\n");
        return;
    }

    h->is_free = 1;
    coalesce(h);
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    block_header_t* h = (block_header_t*)((uintptr_t)ptr - sizeof(block_header_t));
    if (h->magic != HEAP_MAGIC) return NULL;

    size_t want = ALIGN_UP(new_size, 16);
    if (h->size >= want) return ptr;

    if (h->next && h->next->is_free && h->next->magic == HEAP_MAGIC) {
        coalesce(h);
        if (h->size >= want) {
            split_block(h, want);
            h->is_free = 0;
            return (void*)((uintptr_t)h + sizeof(block_header_t));
        }
    }

    void* np = kmalloc(want);
    if (!np) return NULL;

    // manual memcpy
    uint8_t* s = (uint8_t*)ptr;
    uint8_t* d = (uint8_t*)np;
    for (size_t i = 0; i < h->size; i++) d[i] = s[i];

    kfree(ptr);
    return np;
}

size_t kheap_free_size(void) {
    size_t total = 0;
    for (block_header_t* cur = heap_start; cur; cur = cur->next) {
        if (cur->magic != HEAP_MAGIC) break;
        if (cur->is_free) total += cur->size;
    }
    return total;
}
