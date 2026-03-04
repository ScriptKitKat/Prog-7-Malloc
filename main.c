#define _POSIX_C_SOURCE 199309L

#include "./libtdmm/tdmm.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/*
 * Test harness for memory allocator statistics.
 *
 * Metrics gathered:
 *   1. Average % memory utilization over a program run
 *   2. % memory utilization as a function of time (per-request)
 *   3. Speed of t_malloc/t_free vs size (1 byte – 8 MiB, log scale)
 *   4. Metadata / data-structure overhead over a program run
 *
 * Three test phases per strategy:
 *   Phase A – "Speed vs Size": malloc+free single allocations from 1B to 8M
 *             (measures metric 3)
 *   Phase B – "Realistic workload": allocate many blocks, free subsets,
 *             re-allocate (measures metrics 1, 2, 4)
 *   Phase C – "Fragmentation stress": allocate many small blocks, free
 *             every other one, then allocate medium blocks
 *             (measures metrics 1, 2, 4 under fragmentation)
 */

static double elapsed_ns(struct timespec *start, struct timespec *end) {
    return (double)(end->tv_sec - start->tv_sec) * 1e9 +
           (double)(end->tv_nsec - start->tv_nsec);
}

/* ── Phase A: Speed vs Size (1 B → 8 MiB) ─────────────────────────── */
static void phase_speed(FILE *f) {
    struct timespec t0, t1;

    /* Use log-spaced sizes: 1, 2, 3, 4, 6, 8, 12, 16, … up to 8 MiB */
    size_t sizes[512];
    int n = 0;

    for (double s = 1.0; s <= 8.0 * 1024 * 1024 && n < 512; s *= 1.05) {
        sizes[n++] = (size_t)s;
    }

    for (int i = 0; i < n; i++) {
        size_t sz = sizes[i];

        /* --- malloc timing --- */
        clock_gettime(CLOCK_MONOTONIC, &t0);
        char *p = (char *)t_malloc(sz);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (!p) continue;
        double malloc_ns = elapsed_ns(&t0, &t1);
        log_state("malloc", (int)sz, malloc_ns, f);

        /* --- free timing --- */
        clock_gettime(CLOCK_MONOTONIC, &t0);
        t_free(p);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double free_ns = elapsed_ns(&t0, &t1);
        log_state("free", (int)sz, free_ns, f);
    }
}

/* ── Phase B: Realistic mixed workload ─────────────────────────────── */
#define PHASE_B_COUNT 2000
static void phase_workload(FILE *f) {
    struct timespec t0, t1;
    void *ptrs[PHASE_B_COUNT] = {0};
    size_t alloc_sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    int nsizes = sizeof(alloc_sizes) / sizeof(alloc_sizes[0]);

    /* Step 1: allocate PHASE_B_COUNT blocks of varying sizes */
    for (int i = 0; i < PHASE_B_COUNT; i++) {
        size_t sz = alloc_sizes[i % nsizes];

        clock_gettime(CLOCK_MONOTONIC, &t0);
        ptrs[i] = t_malloc(sz);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("malloc", (int)sz, elapsed_ns(&t0, &t1), f);
    }

    /* Step 2: free every 3rd block (creates holes / fragmentation) */
    for (int i = 0; i < PHASE_B_COUNT; i += 3) {
        if (!ptrs[i]) continue;
        size_t sz = alloc_sizes[i % nsizes];

        clock_gettime(CLOCK_MONOTONIC, &t0);
        t_free(ptrs[i]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("free", (int)sz, elapsed_ns(&t0, &t1), f);
        ptrs[i] = NULL;
    }

    /* Step 3: re-allocate into the holes with slightly different sizes */
    for (int i = 0; i < PHASE_B_COUNT; i += 3) {
        size_t sz = alloc_sizes[(i + 1) % nsizes] + 16; /* offset size */

        clock_gettime(CLOCK_MONOTONIC, &t0);
        ptrs[i] = t_malloc(sz);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("malloc", (int)sz, elapsed_ns(&t0, &t1), f);
    }

    /* Step 4: free everything */
    for (int i = 0; i < PHASE_B_COUNT; i++) {
        if (!ptrs[i]) continue;
        size_t sz = alloc_sizes[i % nsizes];

        clock_gettime(CLOCK_MONOTONIC, &t0);
        t_free(ptrs[i]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("free", (int)sz, elapsed_ns(&t0, &t1), f);
        ptrs[i] = NULL;
    }
}

/* ── Phase C: Fragmentation stress test ────────────────────────────── */
#define PHASE_C_COUNT 4000
static void phase_fragmentation(FILE *f) {
    struct timespec t0, t1;
    void *ptrs[PHASE_C_COUNT] = {0};

    /* Step 1: allocate many small blocks (64 bytes each) */
    for (int i = 0; i < PHASE_C_COUNT; i++) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        ptrs[i] = t_malloc(64);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("malloc", 64, elapsed_ns(&t0, &t1), f);
    }

    /* Step 2: free every other block → maximum fragmentation */
    for (int i = 0; i < PHASE_C_COUNT; i += 2) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        t_free(ptrs[i]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("free", 64, elapsed_ns(&t0, &t1), f);
        ptrs[i] = NULL;
    }

    /* Step 3: allocate medium blocks (256 bytes) — will they fit? */
    void *med_ptrs[PHASE_C_COUNT / 4];
    int med_count = 0;
    for (int i = 0; i < PHASE_C_COUNT / 4; i++) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        med_ptrs[i] = t_malloc(256);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (med_ptrs[i]) med_count++;
        log_state("malloc", 256, elapsed_ns(&t0, &t1), f);
    }

    /* Step 4: free everything remaining */
    for (int i = 0; i < PHASE_C_COUNT; i++) {
        if (!ptrs[i]) continue;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        t_free(ptrs[i]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("free", 64, elapsed_ns(&t0, &t1), f);
    }
    for (int i = 0; i < med_count; i++) {
        if (!med_ptrs[i]) continue;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        t_free(med_ptrs[i]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        log_state("free", 256, elapsed_ns(&t0, &t1), f);
    }
}

/* ── Run all phases for one strategy ───────────────────────────────── */
static int run_strategy(alloc_strat_e strat, const char *name) {
    char fname_speed[128], fname_workload[128], fname_frag[128];
    snprintf(fname_speed,    sizeof(fname_speed),    "%s_speed.csv",    name);
    snprintf(fname_workload, sizeof(fname_workload), "%s_workload.csv", name);
    snprintf(fname_frag,     sizeof(fname_frag),     "%s_frag.csv",     name);

    /* Phase A – speed vs size */
    t_init(strat);
    FILE *f = fopen(fname_speed, "w");
    if (!f) { perror("fopen"); return 1; }
    phase_speed(f);
    fclose(f);

    /* Phase B – realistic workload */
    t_init(strat);
    f = fopen(fname_workload, "w");
    if (!f) { perror("fopen"); return 1; }
    phase_workload(f);
    fclose(f);

    /* Phase C – fragmentation stress */
    t_init(strat);
    f = fopen(fname_frag, "w");
    if (!f) { perror("fopen"); return 1; }
    phase_fragmentation(f);
    fclose(f);

    printf("[%s] Done.\n", name);
    return 0;
}

int main(void) {
    run_strategy(FIRST_FIT, "first_fit");
    run_strategy(BEST_FIT,  "best_fit");
    run_strategy(WORST_FIT, "worst_fit");
	run_strategy(BUDDY,  "buddy_fit");
    run_strategy(MIXED, "mixed_fit");
    printf("All tests complete.\n");
    return 0;
}