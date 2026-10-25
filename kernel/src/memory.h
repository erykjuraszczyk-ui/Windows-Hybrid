#ifndef HYBRID_MEMORY_H
#define HYBRID_MEMORY_H
#include <stdbool.h>
#include <stdint.h>
#include <limine.h>
#define HYBRID_PAGE_SIZE UINT64_C(4096)
bool memory_init(const struct limine_memmap_response *map, uint64_t hhdm_offset);
bool memory_alloc_page(uint64_t *physical_address);
bool memory_free_page(uint64_t physical_address);
void *memory_phys_to_virt(uint64_t physical_address);
uint64_t memory_free_pages(void);
uint64_t memory_allocated_pages(void);
bool memory_self_test(void);
#endif
