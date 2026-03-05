#ifndef TDMM_H
#define TDMM_H

#include <stddef.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <math.h>

typedef enum {
  FIRST_FIT,
  BEST_FIT,
  WORST_FIT,
  BUDDY,
  MIXED,
} alloc_strat_e;

typedef struct MemoryBlock{
  size_t size;
  int is_free; // 1 if free, 0 if allocated
  struct MemoryBlock* next;
  struct MemoryBlock* prev;
} MemoryBlock;

typedef struct AllocatorState{
  alloc_strat_e strategy;
  MemoryBlock* heap_list;
  size_t system_size;
  size_t allocated_size;
  size_t metadata_size;
  int mixed_counter; // for mixed strategy
} AllocatorState;

/**
 * Initializes the memory allocator with the given strategy.
 *
 * @param strat The strategy to use for memory allocation.
 */
void t_init(alloc_strat_e strat);

/**
 * Allocates a block of memory of the given size.
 *
 * @param size The size of the memory block to allocate.
 * @return A pointer to the allocated memory block fails.
 */
void *t_malloc(size_t size);

/**
 * Frees the given memory block.
 *
 * @param ptr The pointer to the memory block to free. This must be a pointer returned by t_malloc.
 */
void t_free(void *ptr);

void* first_fit(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed);
void* best_fit(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed);
void* worst_fit(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed);
void* buddy_alloc(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed);

void log_state(char* type, int i, double time, FILE* fptr);

#endif // TDMM_H
