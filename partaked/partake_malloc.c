/*
 * Local memory allocation for partaked
 *
 *
 * Copyright (C) 2020, The Board of Regents of the University of Wisconsin
 * System
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "partake_malloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void *rainy_day_page;


void partake_initialize_malloc(void) {
    if (rainy_day_page == NULL) {
        rainy_day_page = malloc(4096);
    }
}


#ifdef __GNUC__
__attribute__((noreturn))
#elif defined(_MSC_VER)
__declspec(noreturn)
#endif
static void out_of_memory(const char *func, size_t n, size_t size) {
    /*
     * There are two ways to handle out of memory conditions: give up, or
     * return an error. As a daemon that needs to be reliable, returning an
     * error might seem like a better idea. However, (1) it is difficult
     * (though not impossible) to guarantee, without allocating additional
     * memory, the delivery of an error to connected clients, and (2) depending
     * on the operating system, it can be rather pointless to try to recover
     * from out of memory (this famously applies to Linux). At least for now we
     * do the simplest thing: abort.
     */

    /*
     * We try to print a message without calling malloc(). Even fputs() may
     * allocate memory, so free up some before calling it (this would be
     * less reliable if we were multithreaded).
     *
     * We include the attempted allocation size so that we can diagnose size
     * computation bugs.
     */

    char message[128] = "partaked: ";
    strcat(message, func);
    strcat(message, ": failed to allocate ");

    char b[32], *p;

    if (strcmp(func, "calloc") == 0) {
        p = b + sizeof(b);
        *--p = '\0';
        while (n > 0) {
            *--p = (char)(n % 10) + '0';
            n /= 10;
        }
        strcat(message, p);
        strcat(message, " * ");
    }

    p = b + sizeof(b);
    *--p = '\0';
    while (size > 0) {
        *--p = (char)(size % 10) + '0';
        size /= 10;
    }
    strcat(message, p);
    strcat(message, " bytes");

    free(rainy_day_page);
    fputs(message, stderr);

    abort();
}


void *partake_malloc(size_t size) {
    void *p = malloc(size);
    if (p == NULL && size == 0) {
        p = malloc(1);
    }
    if (p == NULL) {
        out_of_memory("malloc", 1, size);
    }
    return p;
}


void *partake_realloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (p == NULL && size == 0) {
        // Some realloc() implementations free when size is 0. Or ptr may have
        // been NULL.
        p = malloc(1);
    }
    if (p == NULL) {
        out_of_memory("realloc", 1, size);
    }
    return p;
}


void *partake_calloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (p == NULL && (n == 0 || size == 0)) {
        p = malloc(1);
    }
    if (p == NULL) {
        out_of_memory("calloc", n, size);
    }
    return p;
}


void partake_free(const void *ptr) {
    free((void *)ptr);
}
