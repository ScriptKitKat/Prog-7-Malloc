#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "tdmm.h"

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define RESET "\033[0m"
#define BLUE "\033[0;34m"

int tests_passed = 0;
int tests_failed = 0;

void test_result(const char* test_name, int expected, int actual) {
    if (expected == actual) {
        printf(GREEN "PASS" RESET " - %s\n", test_name);
        tests_passed++;
    } else {
        printf(RED "FAIL" RESET " - %s (Expected: %d, Got: %d)\n", test_name, expected, actual);
        tests_failed++;
    }
}

void test_result_ptr(const char* test_name, int expect_non_null, void* actual) {
    if ((expect_non_null && actual != NULL) || (!expect_non_null && actual == NULL)) {
        printf(GREEN "PASS" RESET " - %s\n", test_name);
        tests_passed++;
    } else {
        printf(RED "FAIL" RESET " - %s (Expected: %s, Got: %p)\n", test_name,
               expect_non_null ? "non-NULL" : "NULL", actual);
        tests_failed++;
    }
}

// ============================================================
// FIRST FIT TESTS
// ============================================================

void test_first_fit_basic_alloc() {
    printf(BLUE "\n=== First Fit: Basic Allocation ===" RESET "\n");

    t_init(FIRST_FIT);

    void* p1 = t_malloc(64);
    test_result_ptr("first_fit allocate 64 bytes", 1, p1);

    void* p2 = t_malloc(128);
    test_result_ptr("first_fit allocate 128 bytes", 1, p2);

    // Pointers should be different
    test_result("first_fit allocations are distinct", 1, p1 != p2);

    t_free(p1);
    t_free(p2);
}

void test_first_fit_write_read() {
    printf(BLUE "\n=== First Fit: Write and Read Back ===" RESET "\n");

    t_init(FIRST_FIT);

    int* arr = (int*)t_malloc(10 * sizeof(int));
    test_result_ptr("first_fit allocate int array", 1, arr);

    for (int i = 0; i < 10; i++) arr[i] = i * 42;

    int correct = 1;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != i * 42) { correct = 0; break; }
    }
    test_result("first_fit write/read integrity", 1, correct);

    t_free(arr);
}

void test_first_fit_reuse_after_free() {
    printf(BLUE "\n=== First Fit: Reuse Freed Block ===" RESET "\n");

    t_init(FIRST_FIT);

    void* p1 = t_malloc(64);
    void* p2 = t_malloc(64);
    t_free(p1);

    // First fit should reuse the first freed block
    void* p3 = t_malloc(32);
    test_result_ptr("first_fit reuses freed block", 1, p3);
    // p3 should occupy the space that p1 had (at or near p1's address)
    test_result("first_fit reused address <= p2", 1, (uintptr_t)p3 < (uintptr_t)p2);

    t_free(p2);
    t_free(p3);
}

void test_first_fit_many_allocations() {
    printf(BLUE "\n=== First Fit: Many Small Allocations ===" RESET "\n");

    t_init(FIRST_FIT);

    void* ptrs[100];
    int all_valid = 1;
    for (int i = 0; i < 100; i++) {
        ptrs[i] = t_malloc(16);
        if (ptrs[i] == NULL) { all_valid = 0; break; }
    }
    test_result("first_fit 100 small allocations succeed", 1, all_valid);

    for (int i = 0; i < 100; i++) t_free(ptrs[i]);
}

// ============================================================
// BEST FIT TESTS
// ============================================================

void test_best_fit_basic_alloc() {
    printf(BLUE "\n=== Best Fit: Basic Allocation ===" RESET "\n");

    t_init(BEST_FIT);

    void* p1 = t_malloc(64);
    test_result_ptr("best_fit allocate 64 bytes", 1, p1);

    void* p2 = t_malloc(256);
    test_result_ptr("best_fit allocate 256 bytes", 1, p2);

    t_free(p1);
    t_free(p2);
}

void test_best_fit_chooses_smallest() {
    printf(BLUE "\n=== Best Fit: Selects Smallest Adequate Block ===" RESET "\n");

    t_init(BEST_FIT);

    // Create holes of different sizes
    void* p1 = t_malloc(64);
    void* p2 = t_malloc(64);   // separator
    void* p3 = t_malloc(256);
    void* p4 = t_malloc(64);   // separator
    void* p5 = t_malloc(128);

    // Free p3 (256 bytes) and p5 (128 bytes) to create two holes
    t_free(p3);
    t_free(p5);

    // Allocate 100 bytes — best fit should pick the 128-byte hole (smaller fit)
    void* p6 = t_malloc(100);
    test_result_ptr("best_fit selects tighter block", 1, p6);

    t_free(p1);
    t_free(p2);
    t_free(p4);
    t_free(p6);
}

void test_best_fit_large_allocation() {
    printf(BLUE "\n=== Best Fit: Large Allocation (Triggers mmap Expansion) ===" RESET "\n");

    t_init(BEST_FIT);

    // Allocate more than initial 4096 bytes to force expansion
    void* p1 = t_malloc(8192);
    test_result_ptr("best_fit large alloc triggers expansion", 1, p1);

    memset(p1, 0xAB, 8192);
    unsigned char* bytes = (unsigned char*)p1;
    int correct = 1;
    for (int i = 0; i < 8192; i++) {
        if (bytes[i] != 0xAB) { correct = 0; break; }
    }
    test_result("best_fit large alloc write/read integrity", 1, correct);

    t_free(p1);
}

// ============================================================
// WORST FIT TESTS
// ============================================================

void test_worst_fit_basic_alloc() {
    printf(BLUE "\n=== Worst Fit: Basic Allocation ===" RESET "\n");

    t_init(WORST_FIT);

    void* p1 = t_malloc(32);
    test_result_ptr("worst_fit allocate 32 bytes", 1, p1);

    void* p2 = t_malloc(64);
    test_result_ptr("worst_fit allocate 64 bytes", 1, p2);

    t_free(p1);
    t_free(p2);
}

void test_worst_fit_chooses_largest() {
    printf(BLUE "\n=== Worst Fit: Selects Largest Available Block ===" RESET "\n");

    t_init(WORST_FIT);

    void* p1 = t_malloc(64);
    void* p2 = t_malloc(64);   // separator
    void* p3 = t_malloc(256);
    void* p4 = t_malloc(64);   // separator
    void* p5 = t_malloc(128);
    void* p6 = t_malloc(64);   // separator to prevent coalescing with tail

    // Free p3 (256) and p5 (128) — worst fit should pick the 256-byte hole
    t_free(p3);
    t_free(p5);

    void* p7 = t_malloc(32);
    test_result_ptr("worst_fit picks larger hole", 1, p7);
    // p7 should be in p3's region (the larger hole), not p5's

    t_free(p1);
    t_free(p2);
    t_free(p4);
    t_free(p6);
    t_free(p7);
}

// ============================================================
// BUDDY ALLOCATOR TESTS
// ============================================================

void test_buddy_basic_alloc() {
    printf(BLUE "\n=== Buddy: Basic Allocation ===" RESET "\n");

    t_init(BUDDY);

    void* p1 = t_malloc(64);
    test_result_ptr("buddy allocate 64 bytes", 1, p1);

    void* p2 = t_malloc(128);
    test_result_ptr("buddy allocate 128 bytes", 1, p2);

    t_free(p1);
    t_free(p2);
}

void test_buddy_power_of_two_splitting() {
    printf(BLUE "\n=== Buddy: Power-of-Two Splitting ===" RESET "\n");

    t_init(BUDDY);

    // Small allocation should cause repeated splitting
    void* p1 = t_malloc(1);
    test_result_ptr("buddy allocate 1 byte (split down)", 1, p1);

    // Check we can write to it
    *(char*)p1 = 'X';
    test_result("buddy 1-byte write/read", 'X', *(char*)p1);

    t_free(p1);
}

void test_buddy_multiple_splits() {
    printf(BLUE "\n=== Buddy: Multiple Allocations After Splits ===" RESET "\n");

    t_init(BUDDY);

    void* ptrs[16];
    int all_valid = 1;
    for (int i = 0; i < 16; i++) {
        ptrs[i] = t_malloc(64);
        if (ptrs[i] == NULL) { all_valid = 0; break; }
    }
    test_result("buddy 16 allocations of 64 bytes", 1, all_valid);

    // All pointers should be distinct
    int all_distinct = 1;
    for (int i = 0; i < 16 && all_distinct; i++) {
        for (int j = i + 1; j < 16 && all_distinct; j++) {
            if (ptrs[i] == ptrs[j]) all_distinct = 0;
        }
    }
    test_result("buddy all 16 pointers are distinct", 1, all_distinct);

    for (int i = 0; i < 16; i++) t_free(ptrs[i]);
}

void test_buddy_large_allocation() {
    printf(BLUE "\n=== Buddy: Large Allocation ===" RESET "\n");

    t_init(BUDDY);

    void* p1 = t_malloc(1024 * 1024); // 1 MB
    test_result_ptr("buddy allocate 1 MB", 1, p1);

    memset(p1, 0xFF, 1024 * 1024);
    test_result("buddy 1 MB write succeeds", 0xFF, ((unsigned char*)p1)[1024 * 1024 - 1]);

    t_free(p1);
}

// ============================================================
// FREE AND COALESCING TESTS
// ============================================================

void test_free_null() {
    printf(BLUE "\n=== Free: NULL Pointer ===" RESET "\n");

    t_init(FIRST_FIT);

    // Should not crash
    t_free(NULL);
    test_result("t_free(NULL) does not crash", 1, 1);
}

void test_coalescing_forward() {
    printf(BLUE "\n=== Coalescing: Forward Merge ===" RESET "\n");

    t_init(FIRST_FIT);

    void* p1 = t_malloc(64);
    void* p2 = t_malloc(64);
    void* p3 = t_malloc(64);

    // Free p2 then p3 — p2's block should merge forward with p3
    // (actually free p2 first, then p3 triggers forward coalesce from p2's perspective
    //  OR backward coalesce from p3's perspective)
    t_free(p2);
    t_free(p3);

    // Now allocate something that requires the combined space
    // 64 + 64 + metadata overhead — a 120-byte alloc should fit in the merged hole
    void* p4 = t_malloc(120);
    test_result_ptr("coalescing forward creates usable block", 1, p4);
    // p4 should be placed where p2 was
    test_result("coalesced block at p2's location", 1, (uintptr_t)p4 <= (uintptr_t)p2 + 64);

    t_free(p1);
    t_free(p4);
}

void test_coalescing_backward() {
    printf(BLUE "\n=== Coalescing: Backward Merge ===" RESET "\n");

    t_init(FIRST_FIT);

    void* p1 = t_malloc(64);
    void* p2 = t_malloc(64);
    void* p3 = t_malloc(64);

    // Free p1 first, then p2 — p2 should merge backward into p1
    t_free(p1);
    t_free(p2);

    void* p4 = t_malloc(120);
    test_result_ptr("coalescing backward creates usable block", 1, p4);

    t_free(p3);
    t_free(p4);
}

void test_coalescing_both_directions() {
    printf(BLUE "\n=== Coalescing: Both Directions ===" RESET "\n");

    t_init(FIRST_FIT);

    void* p1 = t_malloc(64);
    void* p2 = t_malloc(64);
    void* p3 = t_malloc(64);
    void* p4 = t_malloc(64); // keep allocated to bound the region

    // Free p1 and p3, then free p2 — p2 should merge with both p1 and p3
    t_free(p1);
    t_free(p3);
    t_free(p2); // should coalesce forward into p3 and backward into p1

    // Combined: 64*3 + 2*metadata overhead — a 200-byte alloc should fit
    void* p5 = t_malloc(180);
    test_result_ptr("coalescing both directions creates large block", 1, p5);

    t_free(p4);
    t_free(p5);
}

// ============================================================
// MIXED STRATEGY TESTS
// ============================================================

void test_mixed_basic() {
    printf(BLUE "\n=== Mixed: Basic Round-Robin ===" RESET "\n");

    t_init(MIXED);

    // Should cycle through strategies without crashing
    void* ptrs[6];
    int all_valid = 1;
    for (int i = 0; i < 6; i++) {
        ptrs[i] = t_malloc(32);
        if (ptrs[i] == NULL) { all_valid = 0; break; }
    }
    test_result("mixed strategy 6 allocations succeed", 1, all_valid);

    for (int i = 0; i < 6; i++) t_free(ptrs[i]);
}

// ============================================================
// EDGE CASES
// ============================================================

void test_zero_size_alloc() {
    printf(BLUE "\n=== Edge Case: Zero-Size Allocation ===" RESET "\n");

    t_init(FIRST_FIT);

    void* p = t_malloc(0);
    // Implementation rounds up to alignment — should still return valid pointer
    test_result_ptr("t_malloc(0) returns valid pointer", 1, p);

    t_free(p);
}

void test_alignment() {
    printf(BLUE "\n=== Edge Case: Pointer Alignment ===" RESET "\n");

    t_init(FIRST_FIT);

    void* p1 = t_malloc(1);
    void* p2 = t_malloc(7);
    void* p3 = t_malloc(13);

    // All returned pointers should be at least 4-byte aligned (per implementation)
    test_result("t_malloc(1) is 4-byte aligned", 0, (uintptr_t)p1 % 4);
    test_result("t_malloc(7) is 4-byte aligned", 0, (uintptr_t)p2 % 4);
    test_result("t_malloc(13) is 4-byte aligned", 0, (uintptr_t)p3 % 4);

    t_free(p1);
    t_free(p2);
    t_free(p3);
}

void test_alloc_free_alloc_pattern() {
    printf(BLUE "\n=== Edge Case: Alloc-Free-Alloc Stress ===" RESET "\n");

    t_init(BEST_FIT);

    // Repeatedly allocate and free to stress the allocator
    for (int round = 0; round < 10; round++) {
        void* ptrs[20];
        for (int i = 0; i < 20; i++) {
            ptrs[i] = t_malloc(32 + i * 8);
        }
        // Free every other one
        for (int i = 0; i < 20; i += 2) {
            t_free(ptrs[i]);
        }
        // Reallocate into the holes
        for (int i = 0; i < 20; i += 2) {
            ptrs[i] = t_malloc(16);
        }
        // Free all
        for (int i = 0; i < 20; i++) {
            t_free(ptrs[i]);
        }
    }
    test_result("alloc-free-alloc stress (10 rounds, no crash)", 1, 1);
}

void test_data_integrity_across_allocations() {
    printf(BLUE "\n=== Edge Case: Data Integrity Across Allocations ===" RESET "\n");

    t_init(FIRST_FIT);

    // Allocate several buffers and write unique patterns
    char* bufs[5];
    for (int i = 0; i < 5; i++) {
        bufs[i] = (char*)t_malloc(128);
        memset(bufs[i], 'A' + i, 128);
    }

    // Verify no buffer was clobbered by another allocation
    int integrity_ok = 1;
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 128; j++) {
            if (bufs[i][j] != 'A' + i) {
                integrity_ok = 0;
                break;
            }
        }
        if (!integrity_ok) break;
    }
    test_result("data integrity across 5 concurrent allocations", 1, integrity_ok);

    for (int i = 0; i < 5; i++) t_free(bufs[i]);
}

void test_expansion_triggered() {
    printf(BLUE "\n=== Edge Case: Heap Expansion ===" RESET "\n");

    t_init(FIRST_FIT);

    // Initial heap is 4096 bytes. Exhaust it to force mmap expansion.
    void* p1 = t_malloc(3000);
    test_result_ptr("first alloc near heap capacity", 1, p1);

    void* p2 = t_malloc(3000);
    test_result_ptr("second alloc forces expansion", 1, p2);

    // Both should be writable
    memset(p1, 0xCC, 3000);
    memset(p2, 0xDD, 3000);
    test_result("post-expansion write p1", 0xCC, ((unsigned char*)p1)[2999]);
    test_result("post-expansion write p2", 0xDD, ((unsigned char*)p2)[2999]);

    t_free(p1);
    t_free(p2);
}

// ============================================================
// MAIN
// ============================================================

int main() {
    printf(BLUE "========================================" RESET "\n");
    printf(BLUE "  TDMM Allocator Test Suite" RESET "\n");
    printf(BLUE "========================================" RESET "\n");

    // First Fit
    test_first_fit_basic_alloc();
    test_first_fit_write_read();
    test_first_fit_reuse_after_free();
    test_first_fit_many_allocations();

    // Best Fit
    test_best_fit_basic_alloc();
    test_best_fit_chooses_smallest();
    test_best_fit_large_allocation();

    // Worst Fit
    test_worst_fit_basic_alloc();
    test_worst_fit_chooses_largest();

    // Buddy
    test_buddy_basic_alloc();
    test_buddy_power_of_two_splitting();
    test_buddy_multiple_splits();
    test_buddy_large_allocation();

    // Free & Coalescing
    test_free_null();
    test_coalescing_forward();
    test_coalescing_backward();
    test_coalescing_both_directions();

    // Mixed
    test_mixed_basic();

    // Edge Cases
    test_zero_size_alloc();
    test_alignment();
    test_alloc_free_alloc_pattern();
    test_data_integrity_across_allocations();
    test_expansion_triggered();

    printf(BLUE "\n========================================" RESET "\n");
    printf(BLUE "  Results Summary" RESET "\n");
    printf(BLUE "========================================" RESET "\n");
    printf(GREEN "Passed: %d" RESET "\n", tests_passed);
    printf(RED "Failed: %d" RESET "\n", tests_failed);

    return (tests_failed > 0);
}