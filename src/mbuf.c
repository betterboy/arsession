
#include "mbuf.h"

#include <errno.h>

#define MIN_BLK_SIZE (4)
#define BLK_FACTOR (4)

//默认的blk大小
#define BLK_SIZE (2048)

static int can_log(mbuf_t *mbuf)
{
    return mbuf->enable_log;
}

inline static void mbuf_blk_init(mbuf_blk_t *blk)
{
    blk->head = blk->tail = blk->buf;
}

void mbuf_reset(mbuf_t *mbuf, uint32_t new_size)
{
    if (mbuf->blk_count > 1 || mbuf->alloc_size <= new_size)
    {
        mbuf_free(mbuf);
        mbuf_init(mbuf, (new_size > mbuf->alloc_size) ? new_size : mbuf->alloc_size);
    }
    else
    {
        assert(mbuf->blk_count == 1);
        mbuf_blk_init(mbuf->blk_enq);
    }
}

mbuf_blk_t *mbuf_add_blk(mbuf_t *mbuf, uint32_t size)
{
    mbuf_blk_t *blk = (mbuf_blk_t *)malloc(sizeof(mbuf_blk_t));
    size = mbuf->hint_size > size ? mbuf->hint_size : size;
    size = BLK_FACTOR * ((size + BLK_FACTOR - 1) / BLK_FACTOR);
    if (size < MIN_BLK_SIZE)
    {
        size = MIN_BLK_SIZE;
    }

    blk->blk_id = mbuf->blk_count++;
    blk->parent = mbuf;
    blk->blk_size = size;
    blk->buf = (char *)malloc(size);
    blk->end = blk->buf + blk->blk_size;
    blk->head = blk->tail = blk->buf;
    blk->next = NULL;

    if (mbuf->blk_enq != NULL)
    {
        mbuf->blk_enq->next = blk;
    }

    mbuf->blk_enq = blk;
    mbuf->alloc_size += size;

    if (mbuf->blk_deq == NULL)
    {
        mbuf->blk_deq = mbuf->blk_enq;
    }

    if (can_log(mbuf))
    {
        printf("add new blk: cnt=%lu,id=%d,size=%lu,total_alloc=%lu,data_size=%lu\n", mbuf->blk_count, blk->blk_id, size, mbuf->alloc_size, mbuf->data_size);
    }

    return blk;
}

void mbuf_init(mbuf_t *mbuf, uint32_t blk_size)
{
    mbuf->hint_size = blk_size == 0 ? BLK_SIZE : blk_size;
    mbuf->data_size = 0;
    mbuf->blk_count = 0;
    mbuf->alloc_size = 0;
    mbuf->blk_enq = mbuf->blk_deq = NULL;

    mbuf->blk_deq = mbuf_add_blk(mbuf, blk_size);
}

void mbuf_free(mbuf_t *mbuf)
{
    mbuf_blk_t *tmp, *blk;
    for (blk = mbuf->blk_deq; blk && (tmp = blk->next, 1); blk = tmp)
    {
        mbuf->blk_count--;
        free(blk->buf);
        free(blk);
    }
}

uint32_t mbuf_add(mbuf_t *mbuf, const char *data, uint32_t size)
{
    if (data == NULL)
        return 0;

    void *p = mbuf_alloc(mbuf, size);
    memcpy(p, data, size);

    return size;
}

uint32_t mbuf_add_span(mbuf_t *mbuf, const void *data, uint32_t size)
{
    mbuf_blk_t *blk = mbuf->blk_enq;
    int cap = MBUF_BLK_CAP(blk);
    char *cdat = (char *)data;
    if (cap < size)
    {
        memcpy(blk->tail, cdat, cap);
        MBUF_ADVANCE(mbuf, blk, cap);
        size -= cap;
        cdat += cap;
        blk = mbuf_add_blk(mbuf, size);
    }

    memcpy(blk->tail, cdat, size);
    MBUF_ADVANCE(mbuf, blk, size);

    return size;
}

uint32_t mbuf_copy(mbuf_t *mbuf, void *buf, uint32_t size)
{
    if (buf == NULL)
        return 0;

    mbuf_blk_t *blk = mbuf->blk_deq;
    uint32_t offset = 0;
    while (blk && size > 0)
    {
        uint32_t payload = blk->tail - blk->head;
        uint32_t data_len = payload < size ? payload : size;
        memcpy((char *)buf + offset, blk->head, data_len);
        offset += data_len;
        size -= data_len;
        blk = blk->next;
    }

    return offset;
}

void mbuf_drain(mbuf_t *mbuf, uint32_t size)
{
    mbuf_blk_t *blk = mbuf->blk_deq;
    while (blk && size > 0)
    {
        uint32_t payload = blk->tail - blk->head;
        uint32_t data_len = payload < size ? payload : size;
        blk->head += data_len;
        mbuf->data_size -= data_len;
        size -= data_len;

        if (MBUF_BLK_DATA_LEN(blk) == 0)
        {
            //free blk
            mbuf_blk_t *tmp = blk;
            blk = mbuf->blk_deq = blk->next;
            mbuf->blk_count--;
            mbuf->alloc_size -= tmp->blk_size;
            if (mbuf->blk_enq == tmp)
            {
                mbuf->blk_enq = NULL;
            }

            free(tmp->buf);
            free(tmp);
        }
    }
}

uint32_t mbuf_remove(mbuf_t *mbuf, void *buf, uint32_t size)
{
    if (buf == NULL)
        return 0;

    uint32_t ret = mbuf_copy(mbuf, buf, size);
    if (ret > 0)
    {
        mbuf_drain(mbuf, ret);
    }

    return ret;
}

const void *mbuf_pullup(mbuf_t *mbuf)
{
    if (mbuf->blk_count <= 0)
        return NULL;

    if (mbuf->blk_count == 1)
    {
        return mbuf->blk_deq->head;
    }

    mbuf_blk_t *blk = (mbuf_blk_t *)malloc(sizeof(mbuf_blk_t));
    blk->blk_id = 0;
    blk->parent = mbuf;
    blk->buf = (char *)malloc(mbuf->data_size);
    blk->blk_size = mbuf->data_size;
    blk->end = blk->buf + blk->blk_size;
    mbuf_blk_init(blk);
    blk->next = NULL;

    uint32_t offset = 0;
    mbuf_blk_t *tblk, *tmp;
    for (tblk = mbuf->blk_deq; tblk && (tmp = tblk->next, 1); tblk = tmp)
    {
        uint32_t len = MBUF_BLK_DATA_LEN(tblk);
        if (len > 0)
        {
            memcpy(blk->buf + offset, blk->head, len);
            offset += len;
        }

        free(tblk->buf);
        free(tblk);
    }

    blk->tail = blk->buf + offset;
    mbuf->blk_enq = mbuf->blk_deq = blk;
    mbuf->blk_count = 1;
    mbuf->alloc_size = blk->blk_size;
}