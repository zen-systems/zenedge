/* kernel/lib/cpp_runtime.cpp - Minimal C++ Runtime for Bare Metal */
#include <stdint.h>
#include <stddef.h>
#include "../arch/x86_64/io.h"

extern "C" {
    void panic(const char* msg) {
        const char *p = msg;
        while(*p) outb(0x3F8, (uint8_t)*p++);
        while(1) __asm__ __volatile__("hlt");
    }

    void console_write(const char *s) {
        while (*s) outb(0x3F8, (uint8_t)*s++);
    }

    void print_hex64(uint64_t v) {
        const char *hex = "0123456789ABCDEF";
        char buf[16];
        for (int i = 0; i < 16; i++) {
            buf[15-i] = hex[v & 0xF];
            v >>= 4;
        }
        outb(0x3F8, '0'); outb(0x3F8, 'x');
        for (int i = 0; i < 16; i++) outb(0x3F8, buf[i]);
    }

    /* kheap uses 32-bit prints */
    void print_hex32(uint32_t v) {
        print_hex64((uint64_t)v);
    }
    
    void print_uint(uint32_t v) {
        if (v == 0) { console_write("0"); return; }
        char buf[16];
        int i = 0;
        while (v > 0) {
            buf[i++] = '0' + (v % 10);
            v /= 10;
        }
        while (--i >= 0) outb(0x3F8, (uint8_t)buf[i]);
    }
}

/* Forward declarations to Kernel Heap */
extern "C" void* kmalloc(size_t size);
extern "C" void kfree(void* ptr);

/* C++ Operators */
void* operator new(size_t size) {
    return kmalloc(size);
}

void* operator new[](size_t size) {
    return kmalloc(size);
}

void operator delete(void* p) {
    kfree(p);
}

void operator delete[](void* p) {
    kfree(p);
}

void operator delete(void* p, size_t) {
    kfree(p);
}

void operator delete[](void* p, size_t) {
    kfree(p);
}

extern "C" void __cxa_pure_virtual() {
    panic("Pure virtual function call");
}

/* Required for global destructors */
extern "C" void* __dso_handle = 0;

extern "C" int __cxa_atexit(void (*)(void*), void*, void*) {
    /* We don't support global destruction yet */
    return 0;
}
