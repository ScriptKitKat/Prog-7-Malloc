#define _POSIX_C_SOURCE 199309L

#include "tdmm.h"

static AllocatorState allocator;
const size_t DEFAULT_PARTITION = 256;

void t_init(alloc_strat_e strat) {
	memset(&allocator, 0, sizeof(AllocatorState));
	allocator.strategy = strat;
	size_t size = 4096 + sizeof(MemoryBlock);
	void* memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (memory == MAP_FAILED) {
		perror("Memory allocation failed\n");
		return;
	}

	MemoryBlock* start = (MemoryBlock*) memory;
	start->is_free = 1;
	start->size = size - sizeof(MemoryBlock);
	start->next = NULL;
	start->prev = NULL;

	allocator.metadata_size = sizeof(MemoryBlock);
	allocator.system_size = size;
	allocator.heap_list = start;
}

void *t_malloc(size_t size) {
	size = (size + 3) & ~0x3;
	size_t size_needed = size + sizeof(MemoryBlock);
	MemoryBlock* ptr = allocator.heap_list;
	MemoryBlock* prev_ptr = NULL;

	if (allocator.strategy == FIRST_FIT) {
		return first_fit(ptr, prev_ptr, size_needed);
	} else if (allocator.strategy == BEST_FIT) {
		return best_fit(ptr, prev_ptr, size_needed);
	} else if (allocator.strategy == WORST_FIT) {
		return worst_fit(ptr, prev_ptr, size_needed);
	} else if (allocator.strategy == BUDDY) {
		return buddy_alloc(ptr, prev_ptr, size_needed);
	} else if (allocator.strategy == MIXED) {
		void* result;
		if (allocator.mixed_counter % 3 == 0) {
			result = first_fit(ptr, prev_ptr, size_needed);
		} else if (allocator.mixed_counter % 3 == 1) {
			result = best_fit(ptr, prev_ptr, size_needed);
		} else {
			result = worst_fit(ptr, prev_ptr, size_needed);
		}
		allocator.mixed_counter++;
		return result;
	} else{
		perror("Invalid strategy!");
		return NULL;
	}
	return NULL;
}

void* buddy_alloc(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed) {
	// finds the size of the smallest block that can fit the requested size + metadata, which is a power of 2. It then searches for a free block of that size or larger.
	size_needed = pow(2, (size_t)ceil(log2(size_needed)));
	MemoryBlock* buddy_ptr = NULL;
	while (ptr != NULL) {
		if (ptr->size >= size_needed && ptr->is_free) {
			if (buddy_ptr == NULL || ptr->size < buddy_ptr->size) {
				buddy_ptr = ptr;
			}
		}

		prev_ptr = ptr;
		ptr = ptr->next;
	}

	if(buddy_ptr == NULL) {
		size_t sizeUp = (size_needed > sizeof(char) * 1024 * 1024) ? size_needed * 2 : sizeof(char) * 1024 * 1024;

		void* hint = NULL;
		if (prev_ptr != NULL && prev_ptr->is_free) {
			hint = (char*)prev_ptr + prev_ptr->size;
		}

		void* memory = mmap(hint, sizeUp, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (memory == MAP_FAILED) return NULL;

		allocator.system_size += sizeUp;

		if (prev_ptr != NULL && memory == hint) {
			prev_ptr->size += sizeUp - sizeof(MemoryBlock);
			buddy_ptr = prev_ptr;
		} else {
			buddy_ptr = (MemoryBlock*) memory;
			buddy_ptr->is_free = 1;
			buddy_ptr->size = sizeUp - sizeof(MemoryBlock);
			buddy_ptr->prev = prev_ptr;
			if(prev_ptr != NULL) prev_ptr->next = buddy_ptr;
			allocator.metadata_size += sizeof(MemoryBlock);
		}
	}

	// split until size is small enough
	// align to 2^n size and ensure that the remaining size after allocation is less than 128 bytes
	while (buddy_ptr->size + sizeof(MemoryBlock) >= size_needed * 2 && buddy_ptr->size / 2 >= DEFAULT_PARTITION) {
		size_t half_size = ((buddy_ptr->size + sizeof(MemoryBlock)) / 2);
		MemoryBlock* new_block = (MemoryBlock*)(((char*)(buddy_ptr + 1) + half_size));
		new_block->size = half_size - sizeof(MemoryBlock);
		new_block->is_free = 1;
		new_block->prev = buddy_ptr;
		new_block->next = buddy_ptr->next;
		if (buddy_ptr->next != NULL) buddy_ptr->next->prev = new_block;
		buddy_ptr->next = new_block;
		buddy_ptr->size = half_size - sizeof(MemoryBlock);
		allocator.metadata_size += sizeof(MemoryBlock);
	}

	buddy_ptr->is_free = 0;

	allocator.allocated_size += buddy_ptr->size;
	return (void*) (buddy_ptr + 1);
}

void* first_fit(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed) {
	while (ptr != NULL) {
		if (ptr->size >= size_needed && ptr->is_free) {
			break;
		}
		prev_ptr = ptr;
		ptr = ptr->next;
	}

	// expand in size
	if(ptr == NULL) {
		size_t sizeUp = (size_needed > sizeof(char) * 4096) ? size_needed * 2 : sizeof(char) * 4096;
		
		void* hint = NULL;
		if (prev_ptr != NULL && prev_ptr->is_free) {
			hint = (char*)prev_ptr + prev_ptr->size + sizeof(MemoryBlock);
		}

		void* memory = mmap(hint, sizeUp, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (memory == MAP_FAILED) return NULL;

		allocator.system_size += sizeUp;

		if (prev_ptr != NULL && memory == hint) {
			prev_ptr->size += sizeUp;
			ptr = prev_ptr;
		} else {
			ptr = (MemoryBlock*) memory;
			ptr->is_free = 1;
			ptr->size = sizeUp - sizeof(MemoryBlock);
			ptr->prev = prev_ptr;
			if(prev_ptr != NULL) prev_ptr->next = ptr;
			allocator.metadata_size += sizeof(MemoryBlock);
		}
	}

	if (ptr->size >= size_needed + 4) {
		MemoryBlock* new_block = (MemoryBlock*)((char*)ptr + size_needed);
		new_block->size = ptr->size - size_needed;
		new_block->is_free = 1;
		new_block->prev = ptr;
		new_block->next = ptr->next;
		if (ptr->next != NULL) ptr->next->prev = new_block;
		ptr->next = new_block;
		ptr->size = size_needed - sizeof(MemoryBlock);
		allocator.metadata_size += sizeof(MemoryBlock);
	}
	
	ptr->is_free = 0;

	allocator.allocated_size += ptr->size;
	return (void*) (ptr + 1);
}

void* best_fit(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed) {
	MemoryBlock* best_ptr = NULL;
	while (ptr != NULL) {
		if (ptr->size >= size_needed && ptr->is_free) {
			if (best_ptr == NULL || ptr->size < best_ptr->size) {
				best_ptr = ptr;
			}
		}

		prev_ptr = ptr;
		ptr = ptr->next;
	}

	// expand in size
	if(best_ptr == NULL) {
		size_t sizeUp = (size_needed > sizeof(char) * 1024 * 1024) ? size_needed * 2 : sizeof(char) * 1024 * 1024;
		void* hint = NULL;
		if (prev_ptr != NULL && prev_ptr->is_free) {
			hint = (char*)prev_ptr + prev_ptr->size + sizeof(MemoryBlock);
		}

		void* memory = mmap(hint, sizeUp, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (memory == MAP_FAILED) return NULL;

		allocator.system_size += sizeUp;

		if (prev_ptr != NULL && memory == hint) {
			prev_ptr->size += sizeUp;
			best_ptr = prev_ptr;
		} else {
			best_ptr = (MemoryBlock*) memory;
			best_ptr->is_free = 1;
			best_ptr->size = sizeUp - sizeof(MemoryBlock);
			best_ptr->prev = prev_ptr;
			if(prev_ptr != NULL) prev_ptr->next = best_ptr;
			allocator.metadata_size += sizeof(MemoryBlock);
		}
	}

	if (best_ptr->size >= size_needed + 4) {
		MemoryBlock* new_block = (MemoryBlock*)((char*)best_ptr + size_needed);
		new_block->size = best_ptr->size - size_needed;
		new_block->is_free = 1;
		new_block->prev = best_ptr;
		if (best_ptr->next != NULL) best_ptr->next->prev = new_block;
		new_block->next = best_ptr->next;
		best_ptr->next = new_block;
		best_ptr->size = size_needed - sizeof(MemoryBlock);
		allocator.metadata_size += sizeof(MemoryBlock);
	}

	best_ptr->is_free = 0;

	allocator.allocated_size += best_ptr->size;
	return (void*) (best_ptr + 1);
}

void* worst_fit(MemoryBlock* ptr, MemoryBlock* prev_ptr, size_t size_needed) {
	MemoryBlock* worst_ptr = NULL;
	while (ptr != NULL) {
		if (ptr->size >= size_needed && ptr->is_free) {
			if (worst_ptr == NULL || ptr->size > worst_ptr->size) {
				worst_ptr = ptr;
			}
		}
		prev_ptr = ptr;
		ptr = ptr->next;
	}

	if (worst_ptr == NULL) {
		size_t sizeUp = (size_needed > sizeof(char) * 1024 * 1024) ? size_needed * 2 : sizeof(char) * 1024 * 1024;
		void* hint = NULL;
		if (prev_ptr != NULL && prev_ptr->is_free) {
			hint = (char*)prev_ptr + prev_ptr->size + sizeof(MemoryBlock);
		}

		void* memory = mmap(hint, sizeUp, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (memory == MAP_FAILED) return NULL;

		allocator.system_size += sizeUp;

		if (prev_ptr != NULL && memory == hint) {
			prev_ptr->size += sizeUp;
			worst_ptr = prev_ptr;
		} else {
			worst_ptr = (MemoryBlock*) memory;
			worst_ptr->is_free = 1;
			worst_ptr->size = sizeUp - sizeof(MemoryBlock);
			worst_ptr->prev = prev_ptr;
			if(prev_ptr != NULL) prev_ptr->next = worst_ptr;
			allocator.metadata_size += sizeof(MemoryBlock);
		}
	}

	if (worst_ptr->size >= size_needed + 4) {
		MemoryBlock* new_block = (MemoryBlock*)((char*)worst_ptr + size_needed);
		new_block->size = worst_ptr->size - size_needed;
		new_block->is_free = 1;
		new_block->prev = worst_ptr;
		new_block->next = worst_ptr->next;
		if (worst_ptr->next != NULL) worst_ptr->next->prev = new_block;
		worst_ptr->next = new_block;
		worst_ptr->size = size_needed - sizeof(MemoryBlock);
		allocator.metadata_size += sizeof(MemoryBlock);
	}

	worst_ptr->is_free = 0;

	allocator.allocated_size += worst_ptr->size;
	return (void*) (worst_ptr + 1);
}

void t_free(void *ptr) {
	if (!ptr) return;
	MemoryBlock* header = (MemoryBlock*)ptr - 1;
	header->is_free = 1;
	allocator.allocated_size -= header->size;

	if (allocator.strategy == BUDDY) {
		int merged = 1;
		while (merged) {
			merged = 0;
			if (header->next != NULL && header->next->is_free && header->size == header->next->size &&
				((char*)header + sizeof(MemoryBlock) + header->size == (char*) header->next)) {
				header->size += sizeof(MemoryBlock) + header->next->size;
				header->next = header->next->next;
				if (header->next) header->next->prev = header;
				allocator.metadata_size -= sizeof(MemoryBlock);
				merged = 1;
			}

			if (header->prev != NULL && header->prev->is_free && header->size == header->prev->size &&
				((char*)header->prev + sizeof(MemoryBlock) + header->prev->size == (char*) header)) {
				header->prev->size += sizeof(MemoryBlock) + header->size;
				if (header->next) header->next->prev = header->prev;
				header->prev->next = header->next;
				allocator.metadata_size -= sizeof(MemoryBlock);
				header = header->prev;
				merged = 1;
			}
		}
	} else {
		// coalescing
		if (header->next != NULL && header->next->is_free) {
			if ((char*)header + sizeof(MemoryBlock) + header->size == (char*) header->next) {
				header->size += sizeof(MemoryBlock) + header->next->size;
				header->next = header->next->next;
				if (header->next) header->next->prev = header;
				allocator.metadata_size -= sizeof(MemoryBlock);
			}
		}
	
		if (header->prev != NULL && header->prev->is_free) {
			if ((char*)header->prev + sizeof(MemoryBlock) + header->prev->size == (char*) header) {
				header->prev->size += sizeof(MemoryBlock) + header->size;
				if (header->next) header->next->prev = header->prev;
				header->prev->next = header->next;
				allocator.metadata_size -= sizeof(MemoryBlock);
			}
		}
	}

}

void log_state(char* type, int i, double time, FILE* fptr) {
	if(fptr == NULL) return;
	// "type", "size", "time_nanoseconds", "allocated", "system", "metadata"
	fprintf(fptr, "%s, %d, %f, %ld, %ld, %ld\n", type, i, time, allocator.allocated_size, allocator.system_size, allocator.metadata_size);
}