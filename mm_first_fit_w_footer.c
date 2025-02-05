/*
 * mm-implicit.c - The simplest allocator that frees and coalesces memory.
 * 
 * Implicit free list:
 *     every block has a header and a footer;
 * First-fit policy;
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

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12) /* Extend heap by this amount (bytes) */
#define MAX(x, y) ((x) > (y)? (x) : (y))
/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))
/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* 
 * mm_init - initialize the malloc package.
 */

static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
static char *heap_listp; // Points to prologue block.

int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
      return -1;
    PUT_WORD(heap_listp, 0); /* Alignment padding */
    PUT_WORD(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT_WORD(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT_WORD(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2*WSIZE);
    
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
      return -1;
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
  PUT_WORD(HDRP(bp), PACK(size, 0));         /* Free block header */
  PUT_WORD(FTRP(bp), PACK(size, 0));         /* Free block footer */
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
  place(bp, asize);
  return bp;
}

static void *find_fit(size_t asize)
{
  // "First-fit" policy:
  for (char *bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) 
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) 
      return bp;
  return NULL;
}

static void place(void *bp, size_t asize)
{
  char *hdrp = HDRP(bp);
  size_t size = GET_SIZE(hdrp);
  size_t diff = size - asize;
  if (diff >= (2 * DSIZE)) {
    // Split
    PUT_WORD(hdrp, PACK(asize, 1));
    PUT_WORD(FTRP(bp), PACK(asize, 1));
    char *next = NEXT_BLKP(bp);
    PUT_WORD(HDRP(next), PACK(diff, 0));
    PUT_WORD(FTRP(next), PACK(diff, 0));
  } else {
    // Allocate header and footer
    PUT_WORD(hdrp, PACK(size, 1));
    PUT_WORD(FTRP(bp), PACK(size, 1));
  }
}

/*
 * mm_free - Dealloc flag bits;
 * coalesce with free neighbors.
 */
void mm_free(void *ptr)
{
  size_t size = GET_SIZE(HDRP(ptr));

  PUT_WORD(HDRP(ptr), PACK(size, 0));
  PUT_WORD(FTRP(ptr), PACK(size, 0));
  coalesce(ptr);
}

static void *coalesce(void *bp)
{
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size = GET_SIZE(HDRP(bp));

  if (prev_alloc && next_alloc) {       /* Case 1 */
    return bp;
  }

  else if (prev_alloc && !next_alloc) { /* Case 2 */
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT_WORD(HDRP(bp), PACK(size, 0));
    PUT_WORD(FTRP(bp), PACK(size,0));
  }

  else if (!prev_alloc && next_alloc) { /* Case 3 */
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT_WORD(FTRP(bp), PACK(size, 0));
    PUT_WORD(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }

  else {                                /* Case 4 */
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT_WORD(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    PUT_WORD(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }
  return bp;
}

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
