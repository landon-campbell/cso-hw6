
#include "tlb.h"
#include <stddef.h>
#include <stdint.h>

// 4-way set associative TLB with 16 sets
#define TLB_WAYS 4
#define TLB_SETS 16

typedef struct {
    int valid;
    size_t tag;
    size_t ppn;
    int lru_counter; // 1 is the most recently used, TLB_WAYS is least recently used
} TLBEntry;

typedef struct {
    TLBEntry ways[TLB_WAYS];
} TLBSet;

static TLBSet tlb_sets[TLB_SETS];

// tlb_clear invalidates all cache lines in the TLB
void tlb_clear() {
    for (int i = 0; i < TLB_SETS; i++) {
        // un-validate all ways in the set
        for (int j = 0; j < TLB_WAYS; j++) {
            // set valid bit to 0
            tlb_sets[i].ways[j].valid = 0;
            // reset LRU counter
            tlb_sets[i].ways[j].lru_counter = 0;
        }
    }
}

// tlb_peek returns 0 if the virtual address does not have a valid mapping in the TLB.
int tlb_peek(size_t va) {
    // extract the page offset, VPN, set index, and tag from the virtual address
    size_t page_offset = va & ((1UL << POBITS) - 1);
    size_t vpn = va >> POBITS;
    size_t set_index = vpn & (TLB_SETS - 1); // Assuming TLB_SETS is a power of 2
    size_t tag = vpn >> 4;

    // get the set
    TLBSet *set = &tlb_sets[set_index];

    // check for a hit
    for (int i = 0; i < TLB_WAYS; i++) {
        // get the entry
        TLBEntry *entry = &set->ways[i];
        // check if the entry is valid and has the same tag
        if (entry->valid && entry->tag == tag) {
            // if so return the LRU counter
            return entry->lru_counter;
        }
    }
    // if no hit, return 0
    return 0;
}

// tlb_translate returns the physical address associated to the virtual address
size_t tlb_translate(size_t va) {
    // extract the page offset, VPN, set index, and tag from the virtual address
    size_t page_offset = va & ((1UL << POBITS) - 1);
    size_t vpn = va >> POBITS;
    size_t va_page_start = va & ~((1UL << POBITS) - 1);
    size_t set_index = vpn & (TLB_SETS - 1);
    size_t tag = vpn >> 4;

    // get the set
    TLBSet *set = &tlb_sets[set_index];
    int hit_index = -1;

    // check for a hit
    for (int i = 0; i < TLB_WAYS; i++) {
        // get the entry
        TLBEntry *entry = &set->ways[i];
        // check if the entry is valid and has the same tag
        if (entry->valid && entry->tag == tag) {
            // if so, save the index
            hit_index = i;
            break;
        }
    }

    // check if there was a hit
    if (hit_index != -1) {
        // cache hit
        int old_lru = set->ways[hit_index].lru_counter;

        // update LRU counters
        for (int i = 0; i < TLB_WAYS; i++) {
            // get the entry
            TLBEntry *entry = &set->ways[i];
            // if the entry is valid and has a lower LRU counter than the hit entry
            if (entry->valid && entry->lru_counter < old_lru) {
                // increment the LRU counter
                entry->lru_counter++;
            }
        }
        // set the LRU counter of the hit entry to 1
        set->ways[hit_index].lru_counter = 1;
        // return the physical address
        size_t pa_page_start = (set->ways[hit_index].ppn) << POBITS;
        return pa_page_start | page_offset;
    } else { // cache miss
        // translate the virtual address
        size_t pa_page_start = translate(va_page_start);
        // if the translation failed, return -1
        if (pa_page_start == (size_t)-1) {
            // Do not update TLB
            return -1;
        }
        // extract the physical page number
        size_t ppn = pa_page_start >> POBITS;

        // Find an empty way or the LRU way to replace
        int replace_index = -1;
        int max_lru_counter = 0;
        // Find an empty way
        for (int i = 0; i < TLB_WAYS; i++) {
            // get the entry
            TLBEntry *entry = &set->ways[i];
            // if the entry is not valid, save the index
            if (!entry->valid) {
                replace_index = i;
                break;
            }
            // if the entry has a higher LRU counter than the current max, save the index
            if (entry->lru_counter > max_lru_counter) {
                max_lru_counter = entry->lru_counter;
                replace_index = i;
            }
        }

        // updaet LRU counters
        for (int i = 0; i < TLB_WAYS; i++) {
            // get the entry
            TLBEntry *entry = &set->ways[i];
            // if the entry is valid, increment the LRU counter
            if (entry->valid) {
                entry->lru_counter++;
                // if the LRU counter is greater than the number of ways, set it to the number of ways
                if (entry->lru_counter > TLB_WAYS) {
                    entry->lru_counter = TLB_WAYS;
                }
            }
        }

        // insert new entry
        TLBEntry *new_entry = &set->ways[replace_index];
        new_entry->valid = 1;
        new_entry->tag = tag;
        new_entry->ppn = ppn;
        new_entry->lru_counter = 1;
        // return the physical address
        return pa_page_start | page_offset;
    }
}
