//一个简单的mbuf实现

#ifndef MBUF_H_
#define MBUF_H_

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

typedef struct mbuf_s mbuf_t;
typedef struct mbuf_blk_s
{
    struct mbuf_blk_s *next;
    //point to mbuf struct
    mbuf_t *parent;

    uint32_t blk_id;
    uint32_t blk_size;
    char *head;
    char *tail;
    char *end;
    char *buf;
} mbuf_blk_t;

struct mbuf_s
{
    // mbuf_blk_t *head;
    // mbuf_blk_t *tail;
    mbuf_blk_t *blk_enq;
    mbuf_blk_t *blk_deq;

    uint32_t blk_count;
    uint32_t data_size;
    uint32_t hint_size;
    uint32_t alloc_size;

    //debug
    int enable_log;
};


#define MBUF_ADVANCE(mbuf, blk, size) do { \
    (mbuf)->data_size += (size); \
    (blk)->tail += (size); \
} while (0)

#define MBUF_BLK_CAP(blk) ((blk)->end - (blk)->tail)
#define MBUF_BLK_DATA_LEN(blk) ((blk)->tail - (blk)->head)

#define MBUF_ENQ(mbuf, data, size) do { \
    void *p = mbuf_alloc(mbuf, size); \
    memcpy(p, data, size); \
} while(0)

#define MBUF_ENQ_WITH_TYPE(mbuf, data, type) do { \
    type *p = (type *)mbuf_alloc(mbuf, sizeof(type)); \
    *p = *data; \
} while(0)

#if defined(__cplusplus)
extern "C" {
#endif

//private
mbuf_blk_t *mbuf_add_blk(mbuf_t *mbuf, uint32_t size);

void mbuf_init(mbuf_t *mbuf, uint32_t blk_size);
void mbuf_free(mbuf_t *mbuf);
void mbuf_reset(mbuf_t *mbuf, uint32_t data_size);
//添加数据，如果当前blk空间不足，会新分配一个
uint32_t mbuf_add(mbuf_t *mbuf, const char *data, uint32_t size);
//添加数据，如果当前blk空间不足，会先填满当前blk，再分配一个新的
uint32_t mbuf_add_span(mbuf_t *mbuf_t, const void *data, uint32_t size);
//copy数据，但是不会从mbuf删除
uint32_t mbuf_copy(mbuf_t *mbuf, void *buf, uint32_t size);
//读取数据，同时会从mbuf中删除
uint32_t mbuf_remove(mbuf_t *mbuf, void *buf, uint32_t size);
//把mbuf内的数据变成连续的空间
const void *mbuf_pullup(mbuf_t *mbuf);
//删除mbuf中的数据
void mbuf_drain(mbuf_t *mbuf, uint32_t size);

inline static void *mbuf_alloc(mbuf_t *mbuf, uint32_t size)
{
    if (mbuf->blk_enq == NULL) {
        mbuf->blk_enq = mbuf_add_blk(mbuf, size);
    }

    for (; (uint32_t)MBUF_BLK_CAP(mbuf->blk_enq) < size; ) {
        if (mbuf->blk_enq->next == NULL) {
            mbuf_add_blk(mbuf, size);
            break;
        } else {
            mbuf->blk_enq = mbuf->blk_enq->next;
        }

        assert(MBUF_BLK_DATA_LEN(mbuf->blk_enq) == 0);
    }

    MBUF_ADVANCE(mbuf, mbuf->blk_enq, size);
    return mbuf->blk_enq->tail - size;
}

#if defined(__cplusplus)
}
#endif

#endif //MBUF_H_