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
    "jungle-2Team",
    /* First member's full name */
    "Seungchan",
    /* First member's email address */
    "Yeeeaaa",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* 메모리 정렬기준 8byte로 설정*/
#define ALIGNMENT 8

/* ALIGNMENT(8)의 배수로 올림 정렬하는 매크로 */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)   /* size를 ALIGNMENT의 배수로 올림 (하위 3비트를 0으로 정렬) */

/* size 크기 받는 매크로 */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 기본설정 매크로 */
#define WSIZE 4  /* 워드 크기: 4바이트 (header/footer용) */
#define DSIZE 8  /* 더블워드 크기: 8바이트 (최소 블록 단위 등) */
#define CHUCKSIZE (1<<12) /* 힙 확장할때 크기 단위 : 2^12 = 4096바이트 */ /* 2^10 일때 */
#define MIN_BLOCK_SIZE (2 * DSIZE)  // 최소 블록 크기 = 헤더 + 푸터 + 최소 페이로드 (8 + 8 = 16)

/* 크기 비교 매크로 */
#define MAX(x, y) ((x)>(y) ? (x) : (y)) /* x, y사이즈 비교해서 큰것 리턴 */

/* 합치는 매크로 */
#define PACK(size, alloc) ((size)|(alloc)) /* 블록 크기(size)와 할당 상태 비트(alloc)를 결합하여 헤더/푸터에 저장할 값 생성 */

/* 주소 읽기 및 삽입 */
#define GET(p) (*(unsigned int *)(p)) /* p가 가리키는 주소의 4바이트 값을 읽어옴 */
#define PUT(p, val) (*(unsigned int *)(p) = (val)) /* p가 가리키는 주소에 4바이트 값을 저장함 */

/* 사이즈 및 주소 얻기 */
#define GET_SIZE(p) (GET(p) & ~0x7) /* p가 가리키는 헤더/푸터에서 블록 크기 추출 (하위 3비트 제거) */
#define GET_ALLOC(p) (GET(p) & 0x1) /* p가 가리키는 헤더/푸터에서 할당 상태 추출 (0: free, 1: allocated) */

/* 헤더 푸터 포인터 반환 */
#define HDRP(bp) ((char *)(bp)-WSIZE) /* payload 포인터(bp) 기준으로 블록 헤더의 주소 반환 (bp 바로 앞) */
#define FTRP(bp) ((char *)(bp)+GET_SIZE(HDRP(bp)) - DSIZE)/* payload 포인터(bp) 기준으로 푸터의 주소 반환 */

/* pre 블록 next 블록 위치 포인터 반환 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) /* bp + 현재 블록의 전체 크기 */
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) /* bp에서 이전 블록의 크기만큼 뒤로 이동하여 이전 블록의 bp 반환 */
/* 전역변수 선언 */
static char *heap_listp; /* heap_listp 힙리스트 선언  */
static void *last_fit = NULL; /* nextfit에서 이전블록 확인을 위한 변수 생성 */


static void *coalesce(void *bp) { 
    size_t prev_alloc, next_alloc;
    size_t size = GET_SIZE(HDRP(bp));

    void *prev_bp = PREV_BLKP(bp);
    void *next_bp = NEXT_BLKP(bp);

    // 🔒 prev가 힙의 앞 경계를 넘지 않도록 보호
    if ((char *)prev_bp < (char *)heap_listp) {
        prev_alloc = 1;
    } else {
        prev_alloc = GET_ALLOC(FTRP(prev_bp));
    }

    // 🔒 next가 에필로그 블록인지 확인
    if (GET_SIZE(HDRP(next_bp)) == 0) {
        next_alloc = 1;
    } else {
        next_alloc = GET_ALLOC(HDRP(next_bp));
    }

    // 🔁 병합 케이스 처리
    if (prev_alloc && next_alloc) { // case 1: 둘 다 할당
        return bp;
    } else if (prev_alloc && !next_alloc) { // case 2: next만 free
        size += GET_SIZE(HDRP(next_bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) { // case 3: prev만 free
        size += GET_SIZE(HDRP(prev_bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(prev_bp), PACK(size, 0));
        bp = prev_bp;
    } else { // case 4: prev와 next 모두 free
        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(FTRP(next_bp));
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));
        bp = prev_bp;
    }

    // 🔄 next-fit을 쓴다면 last_fit 보정 (optional)
    last_fit = bp;

    return bp;
}


static void *extend_heap(size_t words){ /* 힙을 재할당 받기 위한 함수 */
    char *bp; /* 힙 안에서 움직일 bp포인트 생성 */
    size_t size; /* 힙 size */

    /* 가용블록 */
    size = (words %2) ? (words+1) * WSIZE : words * WSIZE; /* 요청한 word 수를 짝수로 맞춰서 double word alignment 유지 */
    if((long)(bp = mem_sbrk(size)) == -1) return NULL; /* mem_sbrk로 부터 추가 힙 생성받음 실패하면 -1반환 */

    /*  */
    PUT(HDRP(bp), PACK(size, 0));   /*가용블록 헤더 */
    PUT(FTRP(bp), PACK(size, 0));   /*가용블록 푸터 */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1)); /* 새로운 에필로그 헤더 */

    /*병합된 주소 리턴*/
    return coalesce(bp); 
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    size_t remaining = csize - asize;

    if (remaining >= 2 * DSIZE) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(remaining, 0));
        PUT(FTRP(next_bp), PACK(remaining, 0));
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }

    last_fit = NEXT_BLKP(bp);  // 항상 다음 블록으로 갱신
}


static void *find_fit(size_t asize) {
    void *bp;

    if (last_fit == NULL) last_fit = heap_listp;

    // ① 1차 탐색: last_fit ~ 힙 끝까지
    for (bp = last_fit; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) {
            last_fit = bp;  // ✅ next 탐색 출발점을 여기로 갱신
            return bp;
        }
    }
    // ② 2차 탐색: heap_listp ~ last_fit 전까지
    for (bp = heap_listp; bp < last_fit; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) {
            last_fit = bp;
            return bp;
        }
    }

    return NULL;  // ❌ 못 찾았을 경우
}

// /* first fit */
// static void *find_fit(size_t asize){
//     /* fistfit search */
//     void *bp;

//     if(last_fit == NULL) last_fit = heap_listp;
//     for (bp = heap_listp; GET_SIZE(HDRP(bp)) >0; bp = NEXT_BLKP(bp)){
//         if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
//             return bp;
//         }
//     }
//     return NULL; /* no fit */
// }

/*
 * mm_init - 메모리 초기화 하는곳 (nextfit)
 */
int mm_init(void)
{
    /* 비어있는 힙 생성 */
    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) {
        return -1;
    }
    PUT(heap_listp, 0);                          /*패딩 */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /*프롤로그 헤더*/
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));/*프롤로그 푸터*/
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); /* 에필로그 해더 */
    heap_listp += (2*WSIZE);
    last_fit = heap_listp;
    /* 최소 힙 사이즈 구현 */
    if(extend_heap(CHUCKSIZE/WSIZE) == NULL) {
        return -1;
    }
    return 0;
}

/* next fit mm_malloc */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    /* */
    if(size == 0) {
        return NULL;
    }

    /*  */
    if(size < DSIZE) {
        asize = 2*DSIZE;
    }else{
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    }

    /*  */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    /* 찾을 수 없을 때 */
    extendsize = MAX(asize, CHUCKSIZE);
    if((bp = extend_heap(extendsize/ WSIZE)) == NULL){
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
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
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr)) - DSIZE;
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}