#include "tlb.h"
#include <stddef.h>
#include <stdint.h>

// 4-way set associative TLB with 16 sets
#define TLB_WAYS 4
#define TLB_SETS 16

typedef struct {
    int valid_bit;
    size_t tlb_tag;
    size_t physical_page_number;
    int lru_counter; // 1 is the most recently used, TLB_WAYS is least recently used
} TLBEntry;

typedef struct {
    TLBEntry ways[TLB_WAYS];
} TLBSet;

static TLBSet tlb_set_array[TLB_SETS];

// tlb_clear invalidates all cache lines in the TLB
void tlb_clear() {
    for (int set_index = 0; set_index < TLB_SETS; set_index++) {
        // Un-validate all ways in the set
        for (int way_index = 0; way_index < TLB_WAYS; way_index++) {
            // Set valid bit to 0
            tlb_set_array[set_index].ways[way_index].valid_bit = 0;
            // Reset LRU counter
            tlb_set_array[set_index].ways[way_index].lru_counter = 0;
        }
    }
}

// tlb_peek returns 0 if the virtual address does not have a valid mapping in the TLB.
int tlb_peek(size_t virtual_address) {
    // Extract the page offset, virtual page number (VPN), set index, and tag from the virtual address
    size_t page_offset = virtual_address & ((1UL << POBITS) - 1);
    size_t virtual_page_number = virtual_address >> POBITS;
    size_t tlb_set_index = virtual_page_number & (TLB_SETS - 1); // Assuming TLB_SETS is a power of 2
    size_t tlb_tag = virtual_page_number >> 4;

    // Get the set
    TLBSet *tlb_set_ptr = &tlb_set_array[tlb_set_index];

    // Check for a hit
    for (int way_index = 0; way_index < TLB_WAYS; way_index++) {
        // Get the entry
        TLBEntry *tlb_entry_ptr = &tlb_set_ptr->ways[way_index];
        // Check if the entry is valid and has the same tag
        if (tlb_entry_ptr->valid_bit && tlb_entry_ptr->tlb_tag == tlb_tag) {
            // If so, return the LRU counter
            return tlb_entry_ptr->lru_counter;
        }
    }
    // If no hit, return 0
    return 0;
}

// tlb_translate returns the physical address associated with the virtual address
size_t tlb_translate(size_t virtual_address) {
    // Extract the page offset, virtual page number (VPN), set index, and tag from the virtual address
    size_t page_offset = virtual_address & ((1UL << POBITS) - 1);
    size_t virtual_page_number = virtual_address >> POBITS;
    size_t virtual_address_page_start = virtual_address & ~((1UL << POBITS) - 1);
    size_t tlb_set_index = virtual_page_number & (TLB_SETS - 1);
    size_t tlb_tag = virtual_page_number >> 4;

    // Get the set
    TLBSet *tlb_set_ptr = &tlb_set_array[tlb_set_index];
    int hit_way_index = -1;

    // Check for a hit
    for (int way_index = 0; way_index < TLB_WAYS; way_index++) {
        // Get the entry
        TLBEntry *tlb_entry_ptr = &tlb_set_ptr->ways[way_index];
        // Check if the entry is valid and has the same tag
        if (tlb_entry_ptr->valid_bit && tlb_entry_ptr->tlb_tag == tlb_tag) {
            // If so, save the index
            hit_way_index = way_index;
            break;
        }
    }

    // Check if there was a hit
    if (hit_way_index != -1) {
        // Cache hit
        int old_lru_counter = tlb_set_ptr->ways[hit_way_index].lru_counter;

        // Update LRU counters
        for (int way_index = 0; way_index < TLB_WAYS; way_index++) {
            // Get the entry
            TLBEntry *tlb_entry_ptr = &tlb_set_ptr->ways[way_index];
            // If the entry is valid and has a lower LRU counter than the hit entry
            if (tlb_entry_ptr->valid_bit && tlb_entry_ptr->lru_counter < old_lru_counter) {
                // Increment the LRU counter
                tlb_entry_ptr->lru_counter++;
            }
        }
        // Set the LRU counter of the hit entry to 1
        tlb_set_ptr->ways[hit_way_index].lru_counter = 1;
        // Return the physical address
        size_t physical_address_page_start = (tlb_set_ptr->ways[hit_way_index].physical_page_number) << POBITS;
        return physical_address_page_start | page_offset;
    } else { // Cache miss
        // Translate the virtual address
        size_t physical_address_page_start = translate(virtual_address_page_start);
        // If the translation failed, return -1
        if (physical_address_page_start == (size_t)-1) {
            // Do not update TLB
            return -1;
        }
        // Extract the physical page number
        size_t physical_page_number = physical_address_page_start >> POBITS;

        // Find an empty way or the LRU way to replace
        int replace_way_index = -1;
        int maximum_lru_counter = 0;
        // Find an empty way or the LRU way
        for (int way_index = 0; way_index < TLB_WAYS; way_index++) {
            // Get the entry
            TLBEntry *tlb_entry_ptr = &tlb_set_ptr->ways[way_index];
            // If the entry is not valid, save the index
            if (!tlb_entry_ptr->valid_bit) {
                replace_way_index = way_index;
                break;
            }
            // If the entry has a higher LRU counter than the current max, save the index
            if (tlb_entry_ptr->lru_counter > maximum_lru_counter) {
                maximum_lru_counter = tlb_entry_ptr->lru_counter;
                replace_way_index = way_index;
            }
        }

        // Update LRU counters
        for (int way_index = 0; way_index < TLB_WAYS; way_index++) {
            // Get the entry
            TLBEntry *tlb_entry_ptr = &tlb_set_ptr->ways[way_index];
            // If the entry is valid, increment the LRU counter
            if (tlb_entry_ptr->valid_bit) {
                tlb_entry_ptr->lru_counter++;
                // If the LRU counter is greater than the number of ways, set it to the number of ways
                if (tlb_entry_ptr->lru_counter > TLB_WAYS) {
                    tlb_entry_ptr->lru_counter = TLB_WAYS;
                }
            }
        }

        // Insert new entry
        TLBEntry *new_tlb_entry = &tlb_set_ptr->ways[replace_way_index];
        new_tlb_entry->valid_bit = 1;
        new_tlb_entry->tlb_tag = tlb_tag;
        new_tlb_entry->physical_page_number = physical_page_number;
        new_tlb_entry->lru_counter = 1;
        // Return the physical address
        return physical_address_page_start | page_offset;
    }
}