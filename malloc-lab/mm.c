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

/* ==================================================================================================================== */

/* 블록 병합 함수 (coalesce)
 * 주어진 블록(bp)을 중심으로 이전/다음 블록이 free 상태일 경우 병합 수행
 * 경계 태그 방식에 기반한 병합 (boundary tag coalescing) */
static void *coalesce(void *bp) { 
    size_t prev_alloc, next_alloc;
    size_t size = GET_SIZE(HDRP(bp)); // 현재 블록의 크기

    void *prev_bp = PREV_BLKP(bp); // 이전 블록의 payload 포인터
    void *next_bp = NEXT_BLKP(bp); // 다음 블록의 payload 포인터


    // prev가 힙의 앞 경계를 넘지 않도록 보호
    if ((char *)prev_bp < (char *)heap_listp) {
        prev_alloc = 1; // 경계 조건 보호: 무조건 할당된 것으로 간주
    } else {
         // prev의 푸터를 보고 할당 여부 확인
        prev_alloc = GET_ALLOC(FTRP(prev_bp));
    }

    // next가 에필로그 블록인지 확인
    if (GET_SIZE(HDRP(next_bp)) == 0) {
        next_alloc = 1;  // 에필로그 블록은 항상 할당된 상태로 취급
    } else {
        next_alloc = GET_ALLOC(HDRP(next_bp));
    }

    //  병합 케이스 처리
    if (prev_alloc && next_alloc) { // case 1: 둘 다 할당
        return bp;
    } else if (prev_alloc && !next_alloc) { // case 2: next만 free
        size += GET_SIZE(HDRP(next_bp));
        PUT(HDRP(bp), PACK(size, 0));        // 새 헤더
        PUT(FTRP(next_bp), PACK(size, 0));   // next의 푸터 → 새 푸터
    } else if (!prev_alloc && next_alloc) { // case 3: prev만 free
        size += GET_SIZE(HDRP(prev_bp));  
        PUT(FTRP(bp), PACK(size, 0));       // 현재 블록의 푸터 → 새 푸터
        PUT(HDRP(prev_bp), PACK(size, 0));  // prev의 헤더 → 새 헤더
        bp = prev_bp;                       // 병합된 블록의 시작 포인터로 이동
    } else { // case 4: prev와 next 모두 free
        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(FTRP(next_bp));
        PUT(HDRP(prev_bp), PACK(size, 0));   // 새 헤더
        PUT(FTRP(next_bp), PACK(size, 0));   // 새 푸터
        bp = prev_bp;                        // 병합된 블록의 시작 포인터로 이동
    }

    // next-fit을 쓴다면 last_fit 보정 (optional)
    last_fit = bp;

    return bp;
}
/* ============================================================================================================= */
/*추가 힙 생성*/
static void *extend_heap(size_t words){ /* 힙을 재할당 받기 위한 함수 */
    char *bp; /* 새로 확장된 가용 블록의 payload 포인터 */
    size_t size; /* 새로 요청할 힙 공간의 크기 (바이트 단위) */

   /* 더블 워드 정렬을 위해 요청한 word 수를 짝수로 맞추고, CHUNKSIZE보다 작으면 CHUNKSIZE만큼 확장하여 최소 단위 보장 */
    size = MAX(ALIGN(words * WSIZE), CHUCKSIZE); 

     /* mem_sbrk로 힙을 size 바이트만큼 확장. 실패 시 -1을 반환하므로 NULL을 리턴하여 오류 처리 */
    if((long)(bp = mem_sbrk(size)) == -1) return NULL;

    /* 확장된 공간을 가용 블록으로 초기화: 헤더 + 푸터 설정 */
    PUT(HDRP(bp), PACK(size, 0));   /* 가용 블록 헤더 (할당 안 됨: alloc bit = 0) */
    PUT(FTRP(bp), PACK(size, 0));   /* 가용 블록 푸터 (헤더와 동일한 정보) */
    
    /* 새로운 에필로그 블록 생성 (size = 0, alloc = 1) */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1)); /* 새 에필로그 헤더 삽입 */

    /* 새로 만든 가용 블록과 이전 블록이 인접해 있으면 병합 수행 */
    return coalesce(bp); 
}

/* =========================================================================================================== */

/* 블럭 배치 함수: 요청한 크기만큼 블록을 배치하고,
 * 남은 공간이 충분하면 분할하여 새 가용 블록 생성 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // 현재 가용 블록의 전체 크기
    size_t remaining = csize - asize;   // 남는 공간 계산

     /* 남은 공간이 4*DSIZE 이상이면 분할:
     * → 왜 4*DSIZE? 최소 블록 크기(헤더+푸터+payload)와 alignment 보장 위해 */
    if (remaining >= 4 * DSIZE) {
         /* 앞쪽 asize만큼 할당 블록 생성 */
        PUT(HDRP(bp), PACK(asize, 1));  // 할당 블록 헤더
        PUT(FTRP(bp), PACK(asize, 1));  // 할당 블록 푸터

        /* 나머지 공간은 새로운 가용 블록으로 설정 */
        void *next_bp = NEXT_BLKP(bp);  // 다음 블록 포인터
        PUT(HDRP(next_bp), PACK(remaining, 0)); // 가용 블록 헤더
        PUT(FTRP(next_bp), PACK(remaining, 0)); // 가용 블록 푸터
    } else {
        /* 남은 공간이 충분하지 않으면 통째로 할당 */
        PUT(HDRP(bp), PACK(csize, 1)); // 전체 블록을 할당 처리
        PUT(FTRP(bp), PACK(csize, 1));
    }
  /* next fit 탐색 포인터 갱신:
    * 다음 탐색 시 현재 할당한 블록의 다음 블록부터 시작 */
    last_fit = NEXT_BLKP(bp); 
}

/* ========================================================================================================== */
/* next-fit (83점) */
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

/* =========================================================================================================== */
// /* best-fit (61점) */
// static void *find_fit(size_t asize){
//     void *bp;
//     void *best_bp = NULL;
//     size_t min_diff = (size_t)-1; /* 가능한 최대값으로 초기화 */


//     for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
//         size_t csize = GET_SIZE(HDRP(bp));
//         if(!GET_ALLOC(HDRP(bp)) && csize >= asize){
//             size_t diff = csize - asize;
//             if(diff < min_diff){
//             min_diff = diff;
//             best_bp = bp;
//             if (diff == 0) return bp;
//         }
//     }
// }
// return best_bp;
// }

/* =========================================================================================================== */
// /* first fit (63점) */
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

/* =================================================================================================================== */
/*
 * mm_init - 메모리 초기화 하는곳
 * 힙의 시작 부분을 정렬, 프롤로그, 에필로그로 구성해 초기화하고
 * 이후 기본 가용 공간 확보를 위해 힙을 확장한다.
 */
int mm_init(void)
{
      /* 초기 힙 공간 4워드(16바이트)를 할당: 
     * 패딩 + 프롤로그 헤더 + 프롤로그 푸터 + 에필로그 헤더 */
    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) {
        return -1;  // sbrk 실패 시 -1 반환
    }
    PUT(heap_listp, 0);                           /* 패딩: 정렬 유지를 위한 공간 (사용되지 않음) */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));  /* 프롤로그 헤더: 크기 8바이트, 할당됨 */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));  /* 프롤로그 푸터: 헤더와 동일 */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));       /* 에필로그 헤더: 크기 0, 할당됨 (가용 블록 탐색의 끝 표시) */
    heap_listp += (2*WSIZE); // 프롤로그 블록의 payload를 가리키도록 포인터 이동
    last_fit = heap_listp;   // next-fit을 위한 시작 위치 초기화
    
    /* 초기 가용 블록을 만들기 위해 힙을 CHUNKSIZE만큼 확장 */
    if(extend_heap(CHUCKSIZE/WSIZE) == NULL) {
        return -1;
    }
    return 0;
}

/* ============================================================================================================================ */

/* next fit mm_malloc */
void *mm_malloc(size_t size)
{
    size_t asize;      // 실제 할당될 블록 크기 (조정된 크기)
    size_t extendsize; // find_fit 실패 시 확장할 힙 크기
    char *bp;          // 블록 포인터

    /* 예외 처리: 요청한 size가 0일 경우 NULL 반환 */
    if(size == 0) {
        return NULL;
    }

    /* 최소 블록 크기 보장 및 8바이트 정렬을 위한 조정
     * - 최소 블록 크기는 header + footer + 최소 payload로 16바이트
     * - 추가 DSIZE는 정렬 padding + 헤더/푸터 감안
     */
    if(size < DSIZE) {
        asize = 2*DSIZE;  // 최소 16바이트
    }else{
         // 정렬을 위한 올림 연산: DSIZE 배수로 올림
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    }

    /* 가용 블록 탐색 (next fit 방식 사용 중일 것) */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);  // 블록 배치 및 분할 처리
        return bp;         // payload 포인터 반환
    }

    /* 적절한 가용 블록이 없을 경우 힙 확장 */
    extendsize = MAX(asize, CHUCKSIZE); // 요청 크기와 기본 확장 단위 중 큰 값 선택
    if((bp = extend_heap(extendsize/ WSIZE)) == NULL){
        return NULL; // 힙 확장 실패
    }

    /* 새로 확장된 블록에 요청 블록 배치 */
    place(bp, asize);
    return bp;
}

/* ================================================================================================================== */

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

/* ================================================================================================================== */
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
/* ======================================================================================================================== */