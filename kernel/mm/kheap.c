/* kernel/mm/kheap.c */
#include <stdint.h>
#include <stddef.h>

#include "kheap.h"
#include "../console.h"

#define HEAP_MAGIC 0xC0FFEE42u
#define ALIGN_UP(x, a)   (((x) + ((a)-1)) & ~((a)-1))
#define ALIGN_DOWN(x, a) ((x) & ~((a)-1))

typedef struct block_header {
    uint32_t magic;
    uint32_t size;              // payload size (bytes)
    struct block_header* next;
    struct block_header* prev;
    uint32_t is_free;
} block_header_t;

static block_header_t* heap_start = NULL;
static uintptr_t heap_base = 0;
static uintptr_t heap_end  = 0;

static int ptr_in_heap(void* p) {
    uintptr_t u = (uintptr_t)p;
    return (u >= heap_base) && (u < heap_end);
}

void kheap_init(void* start_addr, size_t size) {
    if (!start_addr) return;

    // Align heap start to 16 for SIMD-friendly alignment (future ORT/wasm needs this).
    uintptr_t base = ALIGN_UP((uintptr_t)start_addr, 16);
    uintptr_t end  = (uintptr_t)start_addr + (uintptr_t)size;
    end = ALIGN_DOWN(end, 16);

    if (end <= base + sizeof(block_header_t) + 16) return;

    heap_base = base;
    heap_end  = end;

    heap_start = (block_header_t*)base;
    heap_start->magic = HEAP_MAGIC;
    heap_start->size  = (uint32_t)(end - base - sizeof(block_header_t));
    heap_start->next  = NULL;
    heap_start->prev  = NULL;
    heap_start->is_free = 1;

    console_write("[kheap] initialized at ");
    print_hex32((uint32_t)heap_base);
    console_write(" size: ");
    print_uint((uint32_t)(heap_end - heap_base));
    console_write(" bytes\n");
}

static void split_block(block_header_t* b, uint32_t want) {
    uint32_t remaining = b->size - want;
    if (remaining <= (uint32_t)(sizeof(block_header_t) + 16)) return;

    uintptr_t nb_addr = (uintptr_t)b + sizeof(block_header_t) + want;
    nb_addr = ALIGN_UP(nb_addr, 16);

    block_header_t* nb = (block_header_t*)nb_addr;
    nb->magic = HEAP_MAGIC;
    nb->is_free = 1;

    uintptr_t b_payload_end = (uintptr_t)b + sizeof(block_header_t) + b->size;
    uint32_t new_size = (uint32_t)(b_payload_end - (uintptr_t)nb - sizeof(block_header_t));

    nb->size = new_size;
    nb->next = b->next;
    nb->prev = b;

    if (b->next) b->next->prev = nb;

    b->next = nb;
    b->size = want;
}

void* kmalloc(size_t size) {
    if (!heap_start || size == 0) return NULL;

    uint32_t want = (uint32_t)ALIGN_UP((uint32_t)size, 16);

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

    console_write("[kheap] OOM! Requested ");
    print_uint(want);
    console_write("\n");
    return NULL;
}

static void coalesce(block_header_t* b) {
    // merge forward
    if (b->next && b->next->is_free && b->next->magic == HEAP_MAGIC) {
        block_header_t* n = b->next;
        b->size = b->size + (uint32_t)((uintptr_t)n - ((uintptr_t)b + sizeof(block_header_t))) + (uint32_t)sizeof(block_header_t) + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
    }

    // merge backward
    if (b->prev && b->prev->is_free && b->prev->magic == HEAP_MAGIC) {
        coalesce(b->prev);
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

    uint32_t want = (uint32_t)ALIGN_UP((uint32_t)new_size, 16);
    if (h->size >= want) return ptr;

    // Try in-place grow: if next is free and enough space, merge then split
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

    // copy old payload
    uint8_t* s = (uint8_t*)ptr;
    uint8_t* d = (uint8_t*)np;
    for (uint32_t i = 0; i < h->size; i++) d[i] = s[i];

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
