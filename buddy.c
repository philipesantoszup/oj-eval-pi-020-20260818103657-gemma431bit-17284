#include "buddy.h"
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define PAGE_SIZE (4 * 1024)
#define MAX_RANK 16

typedef struct free_block {
    struct free_block *next;
    struct free_block *prev;
} free_block_t;

static free_block_t *free_lists[MAX_RANK + 1];
static void *base_ptr = NULL;
static int total_pages = 0;
static unsigned char *page_ranks = NULL;
static bool *page_allocated = NULL;

static int get_page_index(void *p) {
    if (base_ptr == NULL) return -1;
    if (p < base_ptr) return -1;
    long offset = (long)p - (long)base_ptr;
    if (offset % PAGE_SIZE != 0) return -1;
    int idx = offset / PAGE_SIZE;
    if (idx < 0 || idx >= total_pages) return -1;
    return idx;
}

static void list_remove(int rank, free_block_t *block) {
    if (block->prev) block->prev->next = block->next;
    else free_lists[rank] = block->next;
    if (block->next) block->next->prev = block->prev;
}

static void list_add(int rank, free_block_t *block) {
    block->next = free_lists[rank];
    block->prev = NULL;
    if (free_lists[rank]) free_lists[rank]->prev = block;
    free_lists[rank] = block;
}

int init_page(void *p, int pgcount) {
    if (!p || pgcount <= 0) return -EINVAL;

    base_ptr = p;
    total_pages = pgcount;

    for (int i = 1; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }

    page_ranks = (unsigned char *)calloc(total_pages, sizeof(unsigned char));
    page_allocated = (bool *)calloc(total_pages, sizeof(bool));

    if (!page_ranks || !page_allocated) return -EINVAL;

    int remaining = total_pages;
    int offset = 0;
    while (remaining > 0) {
        int rank = MAX_RANK;
        while (rank >= 1) {
            int size = (1 << (rank - 1));
            if (remaining >= size && (offset % size == 0)) {
                free_block_t *block = (free_block_t *)((char *)base_ptr + (size_t)offset * PAGE_SIZE);
                list_add(rank, block);
                page_ranks[offset] = rank;
                offset += size;
                remaining -= size;
                goto next_block;
            }
            rank--;
        }
        next_block:;
    }

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) return ERR_PTR(-EINVAL);

    for (int r = rank; r <= MAX_RANK; r++) {
        if (free_lists[r]) {
            free_block_t *block = free_lists[r];
            list_remove(r, block);

            while (r > rank) {
                r--;
                int size = (1 << (r - 1));
                free_block_t *buddy = (free_block_t *)((char *)block + (size_t)size * PAGE_SIZE);
                list_add(r, buddy);
                page_ranks[get_page_index(buddy)] = r;
            }

            int idx = get_page_index(block);
            page_ranks[idx] = rank;
            page_allocated[idx] = true;
            return block;
        }
    }

    return ERR_PTR(-ENOSPC);
}

int return_pages(void *p) {
    if (!p) return -EINVAL;
    int idx = get_page_index(p);
    if (idx == -1 || !page_allocated[idx]) return -EINVAL;

    int rank = page_ranks[idx];
    page_allocated[idx] = false;

    while (rank < MAX_RANK) {
        int size = (1 << (rank - 1));
        int buddy_idx = idx ^ size;

        if (buddy_idx < 0 || buddy_idx >= total_pages) break;
        if (page_allocated[buddy_idx] || page_ranks[buddy_idx] != rank) break;

        free_block_t *buddy = (free_block_t *)((char *)base_ptr + (size_t)buddy_idx * PAGE_SIZE);
        list_remove(rank, buddy);

        idx = idx & ~size;
        rank++;
    }

    free_block_t *block = (free_block_t *)((char *)base_ptr + (size_t)idx * PAGE_SIZE);
    list_add(rank, block);
    page_ranks[idx] = rank;

    return OK;
}

int query_ranks(void *p) {
    if (!p) return -EINVAL;
    int idx = get_page_index(p);
    if (idx == -1) return -EINVAL;
    return page_ranks[idx];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    int count = 0;
    free_block_t *curr = free_lists[rank];
    while (curr) {
        count++;
        curr = curr->next;
    }
    return count;
}
