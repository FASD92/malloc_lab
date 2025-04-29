#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

/* 기본 상수와 매크로 */
#define WSIZE 8               /* 워드 크기 (8바이트) */
#define DSIZE 16              /* 더블워드 크기 (16바이트) */
#define CHUNKSIZE (1 << 12)    /* 초기 힙 확장 크기 (4096 bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* 패킹과 언패킹 매크로 */
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned long *)(p))
#define PUT(p, val) (*(unsigned long *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* 명시적 가용 리스트 포인터 매크로 */
#define PREV_FREE(bp) (*(char **)((char *)(bp) + 0))
#define NEXT_FREE(bp) (*(char **)((char *)(bp) + WSIZE))

/* 전역 변수 */
static char *heap_listp = NULL;
static char *free_listp = NULL;

/* 함수 프로토타입 */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

/* 팀 정보 */
team_t team = {
"KRAFTON JUNGLE 8th 301",
"CWS",
"hello@world.com",
"",
""
};

/* 초기화 함수 */
int mm_init(void)
{
if ((heap_listp = mem_sbrk(6 * WSIZE)) == (void *)-1)
    return -1;

PUT(heap_listp, 0); /* Alignment padding */
PUT(heap_listp + (1 * WSIZE), PACK(4 * WSIZE, 1)); /* Prologue header (size 32 bytes) */
PUT(heap_listp + (2 * WSIZE), 0); /* Pred 초기화 */
PUT(heap_listp + (3 * WSIZE), 0); /* Succ 초기화 */
PUT(heap_listp + (4 * WSIZE), PACK(4 * WSIZE, 1)); /* Prologue footer */
PUT(heap_listp + (5 * WSIZE), PACK(0, 1)); /* Epilogue header */

heap_listp += (2 * WSIZE);
free_listp = NULL;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/* 힙 확장 */
static void *extend_heap(size_t words)
{
char *bp;
size_t size;

size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
size = MAX(size, 4 * WSIZE); // 최소 블록 크기: header + pred + succ + footer
if ((bp = mem_sbrk(size)) == (void *)-1)
    return NULL;

PUT(HDRP(bp), PACK(size, 0));          /* Free block header */
PUT(FTRP(bp), PACK(size, 0));          /* Free block footer */
NEXT_FREE(bp) = NULL; /* 새 free block pred pointer 초기화 */
PREV_FREE(bp) = NULL; /* 새 free block succ pointer 초기화 */
PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   /* New epilogue header */

    return coalesce(bp);
}

/* 블록 통합 */
static void *coalesce(void *bp)
{
size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
size_t next_alloc = (GET_SIZE(HDRP(NEXT_BLKP(bp))) == 0) ? 1 : GET_ALLOC(HDRP(NEXT_BLKP(bp)));
size_t size = GET_SIZE(HDRP(bp));

if (prev_alloc && next_alloc) {
    insert_free_block(bp);
    return bp;
}
else if (prev_alloc && !next_alloc) {
    remove_free_block(NEXT_BLKP(bp));
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    insert_free_block(bp);
    return bp;
}
else if (!prev_alloc && next_alloc) {
    remove_free_block(PREV_BLKP(bp));
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    bp = PREV_BLKP(bp);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    insert_free_block(bp);
    return bp;
}
else {
    remove_free_block(PREV_BLKP(bp));
    remove_free_block(NEXT_BLKP(bp));
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
    bp = PREV_BLKP(bp);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    insert_free_block(bp);
    return bp;
}
}

/* malloc */
void *mm_malloc(size_t size)
{
size_t asize;            // 정렬된 블록 크기
size_t extendsize;       // 힙에 요청할 크기
char *bp;

if (size == 0)
    return NULL;

if (size <= DSIZE)
    asize = 2 * DSIZE; // 최소 블록 크기 (헤더+푸터+포인터)
else
    asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

if ((bp = find_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
}

extendsize = MAX(asize, CHUNKSIZE);
if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
    return NULL;
place(bp, asize);
return bp;
}

/* free */
void mm_free(void *bp)
{
if (bp == NULL)
    return;

    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

void *mm_realloc(void *ptr, size_t size)
{
if (ptr == NULL)
    return mm_malloc(size);
if (size == 0) {
    mm_free(ptr);
    return NULL;
}

void *newptr;
size_t oldsize = GET_SIZE(HDRP(ptr));
size_t asize;

// 새 size를 8바이트 정렬에 맞추기
if (size <= DSIZE)
    asize = 2 * DSIZE;
else
    asize = DSIZE * ((size + (DSIZE)+(DSIZE - 1)) / DSIZE);

// case 1. 새 요청이 현재 블록보다 작거나 같을 때
if (asize <= oldsize) {
    size_t remaining = oldsize - asize;
    if (remaining >= (2 * DSIZE)) { // 최소 블록 크기 이상이면 split
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));

        void *next_bp = NEXT_BLKP(ptr);
        PUT(HDRP(next_bp), PACK(remaining, 0));
        PUT(FTRP(next_bp), PACK(remaining, 0));
        NEXT_FREE(next_bp) = NULL;
        PREV_FREE(next_bp) = NULL;
        insert_free_block(next_bp);
    }
    return ptr;
}

// case 2. 새 요청이 현재 블록보다 크고, next block이 free 블록이면 병합 시도
void *next_bp = NEXT_BLKP(ptr);
size_t next_alloc = GET_ALLOC(HDRP(next_bp));
size_t next_size = GET_SIZE(HDRP(next_bp));

if (!next_alloc && (oldsize + next_size) >= asize) {
    remove_free_block(next_bp);

    size_t total_size = oldsize + next_size;
    size_t remaining = total_size - asize;

    if (remaining >= (2 * DSIZE)) { // 합친 후에도 split 가능
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));

        void *split_bp = NEXT_BLKP(ptr);
        PUT(HDRP(split_bp), PACK(remaining, 0));
        PUT(FTRP(split_bp), PACK(remaining, 0));
        NEXT_FREE(split_bp) = NULL;
        PREV_FREE(split_bp) = NULL;
        insert_free_block(split_bp);
    } else {
        PUT(HDRP(ptr), PACK(total_size, 1));
        PUT(FTRP(ptr), PACK(total_size, 1));
    }

    return ptr;
}

// case 3. 확장할 수 없으면 새로 malloc
newptr = mm_malloc(size);
if (newptr == NULL)
    return NULL;

size_t copySize = oldsize - DSIZE;
if (size < copySize)
    copySize = size;
memcpy(newptr, ptr, copySize);
mm_free(ptr);
return newptr;
}

/* 가용 리스트에 블록 추가 */
static void insert_free_block(void *bp)
{
NEXT_FREE(bp) = free_listp;
PREV_FREE(bp) = NULL;
if (free_listp != NULL)
    PREV_FREE(free_listp) = bp;
free_listp = bp;
}

/* 가용 리스트에서 블록 제거 */
static void remove_free_block(void *bp)
{
if (PREV_FREE(bp))
    NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
else
    free_listp = NEXT_FREE(bp);

if (NEXT_FREE(bp))
    PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
}

static void *find_fit(size_t asize)
{
void *bp;
void *best_bp = NULL;
size_t best_size = (size_t)-1;

for (bp = free_listp; bp != NULL; bp = NEXT_FREE(bp)) {
    size_t bsize = GET_SIZE(HDRP(bp));
    if (asize <= bsize && (bsize < best_size)) {
        best_size = bsize;
        best_bp = bp;
    }
}
return best_bp;
}

/* 블록 할당 */
static void place(void *bp, size_t asize)
{
size_t csize = GET_SIZE(HDRP(bp));
remove_free_block(bp);

if ((csize - asize) >= (4 * WSIZE)) { // 최소 블록 크기 32바이트 남을 때만 분할
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    void *next_bp = NEXT_BLKP(bp);
    PUT(HDRP(next_bp), PACK(csize - asize, 0));
    PUT(FTRP(next_bp), PACK(csize - asize, 0));
    NEXT_FREE(next_bp) = NULL; /* Pred 초기화 */
    PREV_FREE(next_bp) = NULL; /* Succ 초기화 */
    insert_free_block(next_bp);
}
else {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
}
}