/*
 * mm-explicit.c - An implementation that has linear complexity in the
 *    number of free blocks, instead of number of all blocks.
 * 
 * Explicit free list:
 * every block has a header and a footer;
 * free blocks have pred and succ fields with 32 bit pointers;
 * First-fit policy w/ (kind of) LIFO list;
 * Split when there's at least a min length left;
 * Coalesce both neighbors using header/footer;
 * Realloc uses malloc and free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {"ateam", "Lucas", "fake@email.com", "", ""};

#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12) /* Extend heap by this amount (bytes) */
#define MAX(x, y) ((x) > (y)? (x) : (y))
/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))
/* Read and write a word at address p */
#define GET_WORD(p) (*(unsigned int *)(p))
#define PUT_WORD(p, val) (*(unsigned int *)(p) = (val))
/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET_WORD(p) & ~0x7)
#define GET_ALLOC(p) (GET_WORD(p) & 0x1)
/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
/* Given free block ptr bp, compute address of its pred and succ fields */
#define PRED(bp) ((char *)(bp))
#define SUCC(bp) ((char *)(bp) + WSIZE)
/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
static char *heap_listp; // Points to prologue block.
static char *free_list; // Points to start of the free list.

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
      return -1;
    PUT_WORD(heap_listp, 0);                          /* Alignment padding */
    PUT_WORD(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT_WORD(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT_WORD(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2*WSIZE);
    
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
      return -1;

    /* Initiallize the free list */
    free_list = heap_listp + (2*WSIZE);
    PUT_WORD(PRED(free_list), (unsigned int) NULL);
    PUT_WORD(SUCC(free_list), (unsigned int) NULL);

    return 0;
}

static void *extend_heap(size_t words)
{
  char *bp;
  size_t size;

  /* Allocate an even number of words to maintain alignment */
  size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
  if ((long)(bp = mem_sbrk(size)) == -1)
    return NULL;

  /* Initialize free block header/footer and the epilogue header */
  PUT_WORD(HDRP(bp), size);                  /* Free block header */
  PUT_WORD(FTRP(bp), size);                  /* Free block footer */
  PUT_WORD(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

  /* Coalesce if the previous block was free */
  return coalesce(bp);
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
  size_t asize;      /* Adjusted block size */
  size_t extendsize; /* Amount to extend heap if no fit */
  char *bp;

  /* Ignore spurious requests */
  if (size == 0)
    return NULL;
  
  /* Adjust block size to include overhead and alignment reqs. */
  if (size <= DSIZE)
    asize = 2*DSIZE;
  else
    asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
  
  /* Search the free list for a fit */
  if ((bp = find_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
  }

  /* No fit found. Get more memory and place the block */
  extendsize = MAX(asize,CHUNKSIZE);
  if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
    return NULL;
  
  /* Add new node to the doubly linked list using LIFO */
  if (free_list != NULL) PUT_WORD(PRED(free_list), (unsigned int) bp);
  PUT_WORD(SUCC(bp), (unsigned int) free_list);
  PUT_WORD(PRED(bp), (unsigned int) NULL);
  free_list = bp;

  place(bp, asize);
  return bp;
}

static void *find_fit(size_t asize)
{
  // "First-fit" policy:
  for (char *bp = free_list; bp != NULL; bp = (char *) GET_WORD(SUCC(bp)))
    if (asize <= GET_SIZE(HDRP(bp))) 
      return bp;
  return NULL;
}

static void place(void *bp, size_t asize)
{
  char *hdrp = HDRP(bp);
  unsigned int pred = GET_WORD(PRED(bp));
  unsigned int succ = GET_WORD(SUCC(bp));
  
  size_t size = GET_SIZE(hdrp);
  size_t diff = size - asize;
  
  if (diff >= (2 * DSIZE)) {
    // Split
    // Allocate header and footer
    PUT_WORD(hdrp, PACK(asize, 1));
    PUT_WORD(FTRP(bp), PACK(asize, 1));
    // Write free block's metadata
    char *next = NEXT_BLKP(bp);
    PUT_WORD(HDRP(next), PACK(diff, 0));
    PUT_WORD(FTRP(next), PACK(diff, 0));
    PUT_WORD(SUCC(next), succ);
    PUT_WORD(PRED(next), pred);
    // Connect the free list back in place, not LIFO
    if (pred != (unsigned int) NULL) PUT_WORD(SUCC(pred), (unsigned int) next);
    else free_list = next;
    if (succ != (unsigned int) NULL) PUT_WORD(PRED(succ), (unsigned int) next);
  } else {
    // Allocate header and footer
    PUT_WORD(hdrp, PACK(size, 1));
    PUT_WORD(FTRP(bp), PACK(size, 1));
    // Connect the free list
    if (pred != (unsigned int) NULL) PUT_WORD(SUCC(pred), (unsigned int) succ);
    else free_list = (char *) succ;
    if (succ != (unsigned int) NULL) PUT_WORD(PRED(succ), (unsigned int) pred);
  }
}

/*
 * mm_free - Free and coalesce with free neighbors.
 */
void mm_free(void *ptr)
{
  size_t size = GET_SIZE(HDRP(ptr));

  PUT_WORD(HDRP(ptr), PACK(size, 0));
  PUT_WORD(FTRP(ptr), PACK(size, 0));
  char *new_ptr = (char *) coalesce(ptr);

  // Connect to list using LIFO
  if (free_list != NULL) PUT_WORD(PRED(free_list), (unsigned int) new_ptr);
  PUT_WORD(SUCC(new_ptr), (unsigned int) free_list);
  PUT_WORD(PRED(new_ptr), (unsigned int) NULL);
  free_list = new_ptr;
}

static void *coalesce(void *bp)
{
  char *prev = PREV_BLKP(bp);
  char *next = NEXT_BLKP(bp);
  size_t prev_alloc = GET_ALLOC(FTRP(prev));
  size_t next_alloc = GET_ALLOC(HDRP(next));
  size_t size = GET_SIZE(HDRP(bp));

  if (prev_alloc && next_alloc) {       /* Case 1 */
    return bp;
  }

  else if (prev_alloc && !next_alloc) { /* Case 2 */
    size += GET_SIZE(HDRP(next));
    PUT_WORD(HDRP(bp), PACK(size, 0));
    PUT_WORD(FTRP(bp), PACK(size,0));

    unsigned int pred = GET_WORD(PRED(next));
    unsigned int succ = GET_WORD(SUCC(next));
    if (pred != (unsigned int) NULL) PUT_WORD(SUCC(pred), (unsigned int) succ);
    else free_list = (char *) succ;
    if (succ != (unsigned int) NULL) PUT_WORD(PRED(succ), (unsigned int) pred);
  }

  else if (!prev_alloc && next_alloc) { /* Case 3 */
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT_WORD(FTRP(bp), PACK(size, 0));
    PUT_WORD(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);

    unsigned int pred = GET_WORD(PRED(prev));
    unsigned int succ = GET_WORD(SUCC(prev));
    if (pred != (unsigned int) NULL) PUT_WORD(SUCC(pred), (unsigned int) succ);
    else free_list = (char *) succ;
    if (succ != (unsigned int) NULL) PUT_WORD(PRED(succ), (unsigned int) pred);
  }

  else {                                /* Case 4 */
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT_WORD(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    PUT_WORD(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
    
    unsigned int pred_next = GET_WORD(PRED(next));
    unsigned int succ_next = GET_WORD(SUCC(next));
    if (pred_next != (unsigned int) NULL) PUT_WORD(SUCC(pred_next), (unsigned int) succ_next);
    else free_list = (char *) succ_next;
    if (succ_next != (unsigned int) NULL) PUT_WORD(PRED(succ_next), (unsigned int) pred_next);

    unsigned int pred_prev = GET_WORD(PRED(prev));
    unsigned int succ_prev = GET_WORD(SUCC(prev));
    if (pred_prev != (unsigned int) NULL) PUT_WORD(SUCC(pred_prev), (unsigned int) succ_prev);
    else free_list = (char *) succ_prev;
    if (succ_prev != (unsigned int) NULL) PUT_WORD(PRED(succ_prev), (unsigned int) pred_prev);
  }
  return bp;
}

// TODO
/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;
  
  newptr = mm_malloc(size);
  if (newptr == NULL)
    return NULL;
  copySize = GET_SIZE(HDRP(oldptr));
  if (size < copySize)
    copySize = size;
  memcpy(newptr, oldptr, copySize);
  mm_free(oldptr);
  return newptr;
}
