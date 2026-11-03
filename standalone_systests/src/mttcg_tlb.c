/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdint.h>
#include <stdio.h>
#include <hexagon_standalone.h>

#define ITERATIONS 2000
#define MAX_SLOTS 64
#define OLD_ASID 81
#define NEW_ASID 82
#define VALID (1ULL << 63)
#define STACK_SZ 4096

static volatile int stop;
static long long thread1_stack[STACK_SZ / sizeof(long long)];
static unsigned slots[MAX_SLOTS];

static uint64_t read_tlb(unsigned index)
{
    uint64_t value;
    asm volatile("%0 = tlbr(%1)" : "=r"(value) : "r"(index) : "memory");
    return value;
}

static void write_tlb(unsigned index, uint64_t value)
{
    asm volatile("tlbw(%0, %1)" :: "r"(value), "r"(index) : "memory");
}

/* 4 KiB entries at virtual addresses that the test does not use. */
static uint64_t entry(unsigned asid, unsigned slot)
{
    return VALID | ((uint64_t)asid << 52) |
           ((uint64_t)(0x60000 + slot) << 32) | 1;
}

static void invalidate_worker(void *unused)
{
    while (!stop) {
        asm volatile("tlbinvasid(%0)" :: "r"(OLD_ASID << 20) : "memory");
    }
}

int main(void)
{
    unsigned count = 0;
    int failed = 0;

    /* Use only invalid entries, so the runtime mappings stay intact. */
    for (unsigned i = 1; i < 128 && count < MAX_SLOTS; i++) {
        if (!(read_tlb(i) & VALID)) {
            slots[count++] = i;
        }
    }
    thread_create(invalidate_worker,
                  &thread1_stack[STACK_SZ / sizeof(long long) - 2], 1, NULL);
    for (unsigned n = 0; n < ITERATIONS && !failed; n++) {
        for (unsigned i = 0; i < count; i++) {
            write_tlb(slots[i], entry(OLD_ASID, i));
        }
        for (unsigned i = 0; i < count; i++) {
            write_tlb(slots[i], entry(NEW_ASID, i));
        }
        for (unsigned i = 0; i < count && !failed; i++) {
            uint64_t value = read_tlb(slots[i]);
            if (value != entry(NEW_ASID, i)) {
                printf("FAIL: TLB[%u]=%016llx, the replacement was lost\n",
                       slots[i], (unsigned long long)value);
                failed = 1;
            }
        }
    }
    stop = 1;
    thread_join(1 << 1);
    if (!failed) {
        puts("PASS");
    }
    return failed;
}
