#include "memory.h"
#include <stddef.h>
#include <stdint.h>
#define PAGE_MASK (HYBRID_PAGE_SIZE - UINT64_C(1))
#define LOW_MEMORY_RESERVED_BYTES UINT64_C(0x100000)
static uint8_t *used_bitmap;
static uint8_t *allocated_bitmap;
static uint64_t hhdm_base;
static uint64_t physical_page_count;
static uint64_t free_page_count;
static uint64_t allocated_page_count;
static uint64_t allocation_hint;
static bool memory_ready;
static bool add_overflows_u64(uint64_t left, uint64_t right) {
    return left > UINT64_MAX - right;
}
static bool bit_is_set(const uint8_t *bitmap, uint64_t index) {
    return (bitmap[index >> 3U] & (uint8_t)(1U << (index & 7U))) != 0U;
}
static void bit_set(uint8_t *bitmap, uint64_t index) {
    bitmap[index >> 3U] |= (uint8_t)(1U << (index & 7U));
}
static void bit_clear(uint8_t *bitmap, uint64_t index) {
    bitmap[index >> 3U] &= (uint8_t)~(uint8_t)(1U << (index & 7U));
}
static void bytes_fill(uint8_t *destination, uint8_t value, uint64_t length) {
    for (uint64_t i = 0; i < length; ++i) destination[i] = value;
}
void *memory_phys_to_virt(uint64_t physical_address) {
    if (add_overflows_u64(hhdm_base, physical_address)) return NULL;
    return (void *)(uintptr_t)(hhdm_base + physical_address);
}
bool memory_init(const struct limine_memmap_response *map, uint64_t hhdm_offset) {
    memory_ready = false;
    used_bitmap = NULL; allocated_bitmap = NULL;
    physical_page_count = 0U; free_page_count = 0U;
    allocated_page_count = 0U; allocation_hint = 0U;
    hhdm_base = hhdm_offset;
    if (map == NULL || map->entries == NULL || map->entry_count == 0U) return false;
    uint64_t highest_address = 0U;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL || entry->length == 0U ||
            add_overflows_u64(entry->base, entry->length)) continue;
        const uint64_t end = entry->base + entry->length;
        if (end > highest_address) highest_address = end;
    }
    physical_page_count = highest_address / HYBRID_PAGE_SIZE;
    if (physical_page_count <= LOW_MEMORY_RESERVED_BYTES / HYBRID_PAGE_SIZE ||
        physical_page_count > UINT64_MAX - 7U) return false;
    const uint64_t bitmap_bytes = (physical_page_count + 7U) / 8U;
    if (bitmap_bytes == 0U || bitmap_bytes > UINT64_MAX / 2U) return false;
    const uint64_t metadata_bytes = bitmap_bytes * 2U;
    if (metadata_bytes > UINT64_MAX - PAGE_MASK) return false;
    const uint64_t metadata_pages = (metadata_bytes + PAGE_MASK) / HYBRID_PAGE_SIZE;
    if (metadata_pages == 0U || metadata_pages >= physical_page_count) return false;
    uint64_t bitmap_physical = 0U;
    bool found_bitmap_region = false;
    const uint64_t minimum_page = LOW_MEMORY_RESERVED_BYTES / HYBRID_PAGE_SIZE;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE ||
            entry->length == 0U || add_overflows_u64(entry->base, entry->length)) continue;
        uint64_t first_page = entry->base / HYBRID_PAGE_SIZE;
        if ((entry->base & PAGE_MASK) != 0U) ++first_page;
        if (first_page < minimum_page) first_page = minimum_page;
        const uint64_t last_page = (entry->base + entry->length) / HYBRID_PAGE_SIZE;
        if (last_page >= first_page && last_page - first_page >= metadata_pages) {
            bitmap_physical = first_page * HYBRID_PAGE_SIZE;
            found_bitmap_region = true;
            break;
        }
    }
    if (!found_bitmap_region) return false;
    uint8_t *metadata = (uint8_t *)memory_phys_to_virt(bitmap_physical);
    if (metadata == NULL) return false;
    used_bitmap = metadata;
    allocated_bitmap = metadata + bitmap_bytes;
    bytes_fill(used_bitmap, 0xFFU, bitmap_bytes);
    bytes_fill(allocated_bitmap, 0x00U, bitmap_bytes);
    /* Only full pages explicitly labelled USABLE may enter the free pool. */
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE ||
            entry->length == 0U || add_overflows_u64(entry->base, entry->length)) continue;
        uint64_t first_page = entry->base / HYBRID_PAGE_SIZE;
        if ((entry->base & PAGE_MASK) != 0U) ++first_page;
        if (first_page < minimum_page) first_page = minimum_page;
        uint64_t last_page = (entry->base + entry->length) / HYBRID_PAGE_SIZE;
        if (last_page > physical_page_count) last_page = physical_page_count;
        for (uint64_t page = first_page; page < last_page; ++page) {
            if (bit_is_set(used_bitmap, page)) {
                bit_clear(used_bitmap, page); ++free_page_count;
            }
        }
    }
    /* The bitmap's own backing pages are permanently reserved. */
    const uint64_t bitmap_first_page = bitmap_physical / HYBRID_PAGE_SIZE;
    for (uint64_t i = 0; i < metadata_pages; ++i) {
        const uint64_t page = bitmap_first_page + i;
        if (page >= physical_page_count) return false;
        if (!bit_is_set(used_bitmap, page)) {
            bit_set(used_bitmap, page); --free_page_count;
        }
    }
    memory_ready = true;
    return free_page_count > 0U;
}
bool memory_alloc_page(uint64_t *physical_address) {
    if (!memory_ready || physical_address == NULL || free_page_count == 0U) return false;
    uint64_t selected = UINT64_MAX;
    for (uint64_t page = allocation_hint; page < physical_page_count; ++page) {
        if (!bit_is_set(used_bitmap, page)) { selected = page; break; }
    }
    if (selected == UINT64_MAX) {
        for (uint64_t page = 0; page < allocation_hint; ++page) {
            if (!bit_is_set(used_bitmap, page)) { selected = page; break; }
        }
    }
    if (selected == UINT64_MAX) return false;
    bit_set(used_bitmap, selected); bit_set(allocated_bitmap, selected);
    --free_page_count; ++allocated_page_count;
    allocation_hint = selected + 1U;
    if (allocation_hint >= physical_page_count) allocation_hint = 0U;
    volatile uint8_t *page_memory =
        (volatile uint8_t *)memory_phys_to_virt(selected * HYBRID_PAGE_SIZE);
    if (page_memory == NULL) {
        bit_clear(used_bitmap, selected); bit_clear(allocated_bitmap, selected);
        ++free_page_count; --allocated_page_count; return false;
    }
    for (uint64_t i = 0; i < HYBRID_PAGE_SIZE; ++i) page_memory[i] = 0U;
    *physical_address = selected * HYBRID_PAGE_SIZE;
    return true;
}
bool memory_free_page(uint64_t physical_address) {
    if (!memory_ready || (physical_address & PAGE_MASK) != 0U) return false;
    const uint64_t page = physical_address / HYBRID_PAGE_SIZE;
    if (page >= physical_page_count || !bit_is_set(allocated_bitmap, page) ||
        !bit_is_set(used_bitmap, page)) return false;
    bit_clear(allocated_bitmap, page); bit_clear(used_bitmap, page);
    ++free_page_count; --allocated_page_count;
    if (page < allocation_hint) allocation_hint = page;
    return true;
}
uint64_t memory_free_pages(void) { return free_page_count; }
uint64_t memory_allocated_pages(void) { return allocated_page_count; }
bool memory_self_test(void) {
    const uint64_t initial_free = free_page_count;
    uint64_t first = 0U, second = 0U;
    if (!memory_alloc_page(&first)) return false;
    if (!memory_alloc_page(&second)) { (void)memory_free_page(first); return false; }
    bool zeroed = first != second;
    volatile uint8_t *first_virtual = (volatile uint8_t *)memory_phys_to_virt(first);
    if (first_virtual == NULL) zeroed = false;
    else {
        for (uint64_t i = 0; i < HYBRID_PAGE_SIZE; ++i) {
            if (first_virtual[i] != 0U) { zeroed = false; break; }
        }
        first_virtual[0] = 0xA5U;
    }
    const bool first_freed = memory_free_page(first);
    const bool double_free_rejected = !memory_free_page(first);
    const bool second_freed = memory_free_page(second);
    return zeroed && first_freed && double_free_rejected && second_freed &&
           free_page_count == initial_free && allocated_page_count == 0U;
}
