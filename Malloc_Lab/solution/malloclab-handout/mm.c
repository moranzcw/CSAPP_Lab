/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "moranzcw",
    /* First member's email address */
    "moranzcw@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

// single word (4) or double word (8) alignment
#define ALIGNMENT   8

// rounds up to the nearest multiple of ALIGNMENT
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

// Size of size_t
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// Size of word and double word
#define WSIZE   4
#define DSIZE   8

// initialize the heap with this size
#define INITCHUNKSIZE (1<<6)    // 64 bytes

// Extend the heap with this size each time
#define CHUNKSIZE (1<<12)       // 4 kb

// Sum of free lists
#define LISTSIZE    16

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

// Pack a size and allocated bit into a word
#define PACK(size, alloc) ((size) | (alloc))

// Read and write a word into address p
#define GET(p)            (*(unsigned int *)(p))
#define PUT(p, val)       (*(unsigned int *)(p) = (val))

// Set a pointer
#define SET_PTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))

// Read the size and allocated fields from address p
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

// Pointer of header and footer
#define HDRP(ptr) ((char *)(ptr) - WSIZE)
#define FTRP(ptr) ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - DSIZE)

// The prev block pointer and the next block pointer.
#define PREV_BLK_PTR(ptr) ((char *)(ptr) - GET_SIZE((char *)(ptr) - DSIZE))
#define NEXT_BLK_PTR(ptr) ((char *)(ptr) + GET_SIZE((char *)(ptr) - WSIZE))

// The successor free block pointer's pointer and the predecessor free block pointer's pointer in the free list.
#define PRED_PTR(ptr) ((char *)(ptr))
#define SUCC_PTR(ptr) ((char *)(ptr) + WSIZE)

// The successor free block pointer and the predecessor free block pointer.
#define PRED(ptr) (*(char **)(ptr))
#define SUCC(ptr) (*(char **)(SUCC_PTR(ptr)))


/* Data structure 

Allocated block:

                            31  30  29  ... 5   4   3   2   1   0
                            +-------------------------+-------+--+
    Header:                 |   Size of the block     |       | A|
        block pointer +-->  +-------------------------+-------+--+
                            |                                    |
                            |   Payload                          |
                            |                                    |
                            +------------------------------------+
                            |   Padding(optional)                |
                            +-------------------------+-------+--+
    Footer:                 |   Size of the block     |       | A|
                            +-------------------------+-------+--+

Free block:

                            31  30  29  ... 5   4   3   2   1   0
                            +-------------------------+-------+--+
    Header:                 |   size of the block     |       | A|
        block pointer +-->  +-------------------------+-------+--+
                            |   pointer to its predecessor       |
                            +------------------------------------+
                            |   pointer to its successor         |
                            +------------------------------------+
                            |                                    |
                            |   Payload                          |
                            |                                    |
                            +------------------------------------+
                            |   Padding(optional)                |
                            +-------------------------+-------+--+
    Footer:                 |   size of the block     |       | A|
                            +-------------------------+-------+--+


Heap:
                            31  30  29  ... 5   4   3   2   1   0
    Start of heap:          +-------------------------+-------+--+
                            |   0(Padding)            |       |  |
                            +-------------------------+-------+--+  <--+
                            |   8                     |       | A|     |
static char *heap_listp +-> +-------------------------+-------+--+     +--  Prologue block
                            |   8                     |       | A|     |
                            +-------------------------+-------+--+  <--+
                            |                                    |
                            |                                    |
                            |   Blocks                           |
                            |                                    |
                            |                                    |
                            +-------------------------+-------+--+  <--+
    Footer:                 |   0                     |       | A|     +--  Epilogue block
                            +-------------------------+-------+--+  <--+   

Segregated free lists:

                            +---+---+---+---+---+---+---+---+---+
                            |   |   |   |   |   |   |   |   |   |
                            +-+-+---+---+---+---+---+---+---+---+
                              |       |           |
                              v       v           v
                            +---+   +---+       +---+
                            |   |   |   |       |   |
                            +---+   +---+       +---+
                              |
                              v
                            +---+
                            |   |
                            +---+
*/


// Segregated free lists
void* segregated_free_lists[LISTSIZE];

// Extend the heap
static void* extend_heap(size_t size);
// Coalesce adjacent free block if exists
static void* coalesce(void *block_ptr);
// Place a block with this size to the free block ptr
static void* place(void *block_ptr, size_t size);
// Insert the free block to the free list
static void insert_node(void *block_ptr);
// Delete the free block from the free list
static void delete_node(void *block_ptr);

// mm_init 
int mm_init(void)
{   
    char *heap; 

    // Initialize the segregated free lists
    for (int i = 0; i < LISTSIZE; i++)
    {
        segregated_free_lists[i] = NULL;
    }

    // Initialize the heap
    if ((long)(heap = mem_sbrk(4 * WSIZE)) == -1)
        return -1;

    // Padding for memory alignment
    PUT(heap, 0);
    // Prologue block
    PUT(heap + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap + (2 * WSIZE), PACK(DSIZE, 1));
    // Epilogue block
    PUT(heap + (3 * WSIZE), PACK(0, 1));

    // Extend the heap to INITCHUNKSIZE
    if (extend_heap(INITCHUNKSIZE) == NULL)
        return -1;

    return 0;
}
 
// mm_malloc
void *mm_malloc(size_t size)
{
    if (size == 0)
        return NULL;
    
    // Memory alignment
    if (size <= DSIZE)
        size = 2 * DSIZE;
    else
        size = ALIGN(size + DSIZE);

    size_t searchsize = size;
    void *ptr = NULL;

    for (int i = 0; i < LISTSIZE; i++)
    {
        // Find free list
        if (((searchsize <= 1) && (segregated_free_lists[i] != NULL)))
        {
            ptr = segregated_free_lists[i];
            // Find free block
            while ((ptr != NULL) && ((GET_SIZE(HDRP(ptr)) < size)))
            {
                ptr = SUCC(ptr);
            }
            if (ptr != NULL)
                break;
        }

        searchsize >>= 1;
    }

    // There are no suitable block in the free lists, extend the heap
    if (ptr == NULL)
    {
        if ((ptr = extend_heap(MAX(size, CHUNKSIZE))) == NULL)
            return NULL;
    }

    // Place the block
    ptr = place(ptr, size);

    return ptr;
}

// mm_free
void mm_free(void *block_ptr)
{
    size_t size = GET_SIZE(HDRP(block_ptr));
    
    // Reset header and footer for current block 
    PUT(HDRP(block_ptr), PACK(size, 0));
    PUT(FTRP(block_ptr), PACK(size, 0));

    // Insert current block to free list
    insert_node(block_ptr);

    // Coalesce adjacent free block if exists
    coalesce(block_ptr);
}

// mm_realloc
void *mm_realloc(void *block_ptr, size_t size)
{
    if (size == 0)
        return NULL;

    // Memory alignment
    if (size <= DSIZE)
        size = 2 * DSIZE;
    else
        size = ALIGN(size + DSIZE);

    // 1. If target size is smaller than or equal to current size, return the block pointer directly
    if (size <= GET_SIZE(HDRP(block_ptr)))
        return block_ptr;

    // 2. Target size is bigger than current size

    // 2.1. If the next block is the epilogue block, extend the heap directly
    if (GET_SIZE(HDRP(NEXT_BLK_PTR(block_ptr))) == 0)
    {
        // Extend the heap
        size_t extend_size = MAX((size - GET_SIZE(HDRP(block_ptr))), CHUNKSIZE);
        if (extend_heap(extend_size) == NULL)
            return NULL;

        // Reset current block
        delete_node(NEXT_BLK_PTR(block_ptr));
        size_t new_size = GET_SIZE(HDRP(block_ptr)) + extend_size;
        PUT(HDRP(block_ptr), PACK(new_size, 1));
        PUT(FTRP(block_ptr), PACK(new_size, 1));
        return block_ptr;
    }

    // 2.2. If the next block is a free block, and the size if enough to resize the new block
    if (!GET_ALLOC(HDRP(NEXT_BLK_PTR(block_ptr))))
    {
        size_t new_size = GET_SIZE(HDRP(NEXT_BLK_PTR(block_ptr))) + GET_SIZE(HDRP(block_ptr));
        if(new_size >= size)
        {
            delete_node(NEXT_BLK_PTR(block_ptr));
            PUT(HDRP(block_ptr), PACK(new_size, 1));
            PUT(FTRP(block_ptr), PACK(new_size, 1));
            return block_ptr;
        }
    }
    
    // 2.3. Allocate a new block with the target size
    void *new_block = mm_malloc(size);
    memcpy(new_block, block_ptr, GET_SIZE(HDRP(block_ptr)));
    mm_free(block_ptr);
    return new_block;
}

static void *extend_heap(size_t size)
{
    void *ptr;
    // Memory alignment
    size = ALIGN(size);
    // Extend the heap
    if ((ptr = mem_sbrk(size)) == (void *)-1)
        return NULL;

    // Set the header and the footer
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    // Set the epilogue block of the heap
    PUT(HDRP(NEXT_BLK_PTR(ptr)), PACK(0, 1));
    // Insert this new free block into the free lists
    insert_node(ptr);
    // If previous block is free, coalesce them
    return coalesce(ptr);
}

static void insert_node(void *block_ptr)
{
    size_t size = GET_SIZE(HDRP(block_ptr));
    int listnumber = 0;
    void *succ_ptr = NULL;
    void *pred_ptr = NULL;

    // Find the corresponding free list for current block
    while ((listnumber < LISTSIZE - 1) && (size > 1))
    {
        size >>= 1;
        listnumber++;
    }

    // Find the insert position for block, keep free list in ascending order
    succ_ptr = segregated_free_lists[listnumber];
    while ((succ_ptr != NULL) && (GET_SIZE(HDRP(succ_ptr)) < size))
    {
        pred_ptr = succ_ptr;
        succ_ptr = SUCC(succ_ptr);
    }

    // There are 4 situations
    if (succ_ptr != NULL)
    {
        // 1. The insert position is neither the beginning nor the end of the list
        if (pred_ptr != NULL)
        {
            // Set current block's predecessor pointer
            SET_PTR(PRED_PTR(block_ptr), pred_ptr);
            // Set current block's successor pointer
            SET_PTR(SUCC_PTR(block_ptr), succ_ptr);
            // Set predecessor block's successor
            SET_PTR(PRED_PTR(succ_ptr), block_ptr);
            // Set successor block's predecessor pointer
            SET_PTR(SUCC_PTR(pred_ptr), block_ptr);
        }
        // 2. The insert position is the beginning of the list
        else
        {
            // Set current block's predecessor pointer
            SET_PTR(PRED_PTR(block_ptr), NULL);
            // Set current block's successor pointer
            SET_PTR(SUCC_PTR(block_ptr), succ_ptr);
            // Set successor block's predecessor pointer
            SET_PTR(PRED_PTR(succ_ptr), block_ptr);
            // Set the beginning pointer of the free list
            segregated_free_lists[listnumber] = block_ptr;
        }
    }
    else
    {
        // 3. The insert position is the end of the list
        if (pred_ptr != NULL)
        {
            // Set current block's predecessor pointer
            SET_PTR(PRED_PTR(block_ptr), pred_ptr);
            // Set current block's successor pointer
            SET_PTR(SUCC_PTR(block_ptr), NULL);
            // Set successor block's predecessor pointer
            SET_PTR(SUCC_PTR(pred_ptr), block_ptr);
        }
        // 4. The list is empty
        else
        {
            // Set current block's predecessor pointer
            SET_PTR(SUCC_PTR(block_ptr), NULL);
            // Set current block's successor pointer
            SET_PTR(PRED_PTR(block_ptr), NULL);
            // Set the beginning pointer of the free list
            segregated_free_lists[listnumber] = block_ptr;
        }
    }
}

static void delete_node(void *block_ptr)
{
    int listnumber = 0;
    size_t size = GET_SIZE(HDRP(block_ptr));

    // Find the corresponding free list for current block
    while ((listnumber < LISTSIZE - 1) && (size > 1))
    {
        size >>= 1;
        listnumber++;
    }

    // There are 4 situations
    if (SUCC(block_ptr) != NULL)
    {
        // 1. The current block is neither the beginning nor the end of the list
        if (PRED(block_ptr) != NULL)
        {
            // Set predecessor block's successor
            SET_PTR(SUCC_PTR(PRED(block_ptr)), SUCC(block_ptr));
            // Set successor block's predecessor
            SET_PTR(PRED_PTR(SUCC(block_ptr)), PRED(block_ptr));
        }
        // 2. The current block is the beginning of the list
        else
        {
            // Set successor block's predecessor
            SET_PTR(PRED_PTR(SUCC(block_ptr)), NULL);
            // Set the beginning pointer of the free list
            segregated_free_lists[listnumber] = SUCC(block_ptr);
        }
    }
    else
    {
        // 3. The current block is the end of the list
        if (PRED(block_ptr) != NULL)
        {
            // Set predecessor block's successor
            SET_PTR(SUCC_PTR(PRED(block_ptr)), NULL);
        }
        // 4. The free list has only one block, the current block
        else
        {
            // Set the beginning pointer of the free list
            segregated_free_lists[listnumber] = NULL;
        }
    }
}


static void *coalesce(void *block_ptr)
{
    _Bool prev_allocated_flag = GET_ALLOC(HDRP(PREV_BLK_PTR(block_ptr)));
    _Bool next_allocated_flag = GET_ALLOC(HDRP(NEXT_BLK_PTR(block_ptr)));
    size_t size = GET_SIZE(HDRP(block_ptr));

    // There are 4 situations
    // 1. The previous block and the next block are both allocated
    if (prev_allocated_flag && next_allocated_flag)
    {
        return block_ptr;
    }
    // 2. The preious block is allocated, but the next block is free
    else if (prev_allocated_flag && !next_allocated_flag)
    {
        // Delete current block and the next block
        delete_node(block_ptr);
        delete_node(NEXT_BLK_PTR(block_ptr));
        // Reset header and footer for the new block
        size += GET_SIZE(HDRP(NEXT_BLK_PTR(block_ptr)));
        PUT(HDRP(block_ptr), PACK(size, 0));
        PUT(FTRP(block_ptr), PACK(size, 0));
    }
    // 3. The previous block is free, but the next block is allocated
    else if (!prev_allocated_flag && next_allocated_flag)
    {
        // Delete the previous block and current block
        delete_node(PREV_BLK_PTR(block_ptr));
        delete_node(block_ptr);
        // Reset header and footer for the new block
        size += GET_SIZE(HDRP(PREV_BLK_PTR(block_ptr)));
        PUT(FTRP(block_ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLK_PTR(block_ptr)), PACK(size, 0));
        // Reset current block's pointer
        block_ptr = PREV_BLK_PTR(block_ptr);
    }
    // 4. The previous block and the next block are both free
    else
    {
        // Delete all three blocks
        delete_node(PREV_BLK_PTR(block_ptr));
        delete_node(block_ptr);
        delete_node(NEXT_BLK_PTR(block_ptr));
        // Reset header and footer for the new block
        size += GET_SIZE(HDRP(PREV_BLK_PTR(block_ptr))) + GET_SIZE(HDRP(NEXT_BLK_PTR(block_ptr)));
        PUT(HDRP(PREV_BLK_PTR(block_ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLK_PTR(block_ptr)), PACK(size, 0));
        // Reset current block's pointer
        block_ptr = PREV_BLK_PTR(block_ptr);
    }

    // Insert this new block into the free lists
    insert_node(block_ptr);

    return block_ptr;
}

static void* place(void *block_ptr, size_t size)
{
    size_t free_size = GET_SIZE(HDRP(block_ptr));
    size_t remaining_size = free_size - size;

    delete_node(block_ptr);

    // The remaining free block is too small, don't split
    if (remaining_size < DSIZE * 2)
    {
        PUT(HDRP(block_ptr), PACK(free_size, 1));
        PUT(FTRP(block_ptr), PACK(free_size, 1));
    }
    // Split
    else
    {
        PUT(HDRP(block_ptr), PACK(size, 1));
        PUT(FTRP(block_ptr), PACK(size, 1));
        PUT(HDRP(NEXT_BLK_PTR(block_ptr)), PACK(remaining_size, 0));
        PUT(FTRP(NEXT_BLK_PTR(block_ptr)), PACK(remaining_size, 0));
        insert_node(NEXT_BLK_PTR(block_ptr));
    }
    return block_ptr;
}