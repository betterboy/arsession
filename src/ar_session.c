//async rpc protocol session

#include "ar_session.h"

#include <limits.h>
#include <float.h>
#include <stdarg.h>

#ifdef MS_WINDOWS
#define __func__ __FUNCTION__
#endif

const int MBUF_INIT_SIZE = 16 * 1024;
const int RAW_SEND_BUF_DEFAULT = 64 * 1024;
const int AUTO_ACK_THREASHHOLD_DEFAULT = 10 * 1024;

void ar_session_debug(ar_session_t *ar_sess)
{
}

static int ar_canlog(ar_session_t *ar_sess)
{
    if (ar_sess->canlog == 0 || ar_sess->write_log == NULL)
        return 0;
    return 1;
}

static void ar_log(ar_session_t *ar_sess, const char *fmt, ...)
{
    char buf[1024];
    va_list argptr;
    va_start(argptr, fmt);
    vsprintf(buf, fmt, argptr);
    va_end(argptr);
    ar_sess->write_log(buf, ar_sess, ar_sess->ud);
}

//create a new ar_session.
//@param max_raw_send_buf_size the max bytes in send buf
ar_session_t *ar_session_new(uint32_t session_id, uint32_t max_raw_send_buf_size, uint32_t auto_ack_threashhold)
{
    max_raw_send_buf_size > 0 ? max_raw_send_buf_size : RAW_SEND_BUF_DEFAULT;
    auto_ack_threashhold > 0 ? auto_ack_threashhold : AUTO_ACK_THREASHHOLD_DEFAULT;

    ar_session_t *ar_sess = (ar_session_t *)malloc(sizeof(ar_session_t));
    if (ar_sess == NULL)
    {
        return ar_sess;
    }

    memset(ar_sess, 0, sizeof(ar_session_t));
    ar_sess->session_id = session_id;
    ar_sess->auto_ack_threashhold = auto_ack_threashhold;
    ar_sess->max_raw_send_buf_size = max_raw_send_buf_size;

    ar_sess->recv_raw_offset = 0;
    ar_sess->remote_raw_offset = 0;
    ar_sess->auto_ack_recv_count = 0;

    ar_sess->send_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    mbuf_init(ar_sess->send_buf, MBUF_INIT_SIZE);

    ar_sess->send_raw_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    mbuf_init(ar_sess->send_raw_buf, MBUF_INIT_SIZE);

    ar_sess->recv_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    mbuf_init(ar_sess->recv_buf, MBUF_INIT_SIZE);

    ar_sess->recv_raw_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    mbuf_init(ar_sess->recv_raw_buf, MBUF_INIT_SIZE);

    ar_sess->canlog = 0;
    ar_sess->write_log = NULL;

    return ar_sess;
}

//free ar_session
void ar_session_free(ar_session_t *ar_sess)
{
    mbuf_free(ar_sess->send_buf);
    mbuf_free(ar_sess->send_raw_buf);
    mbuf_free(ar_sess->recv_buf);
    mbuf_free(ar_sess->recv_raw_buf);

    free(ar_sess);
}

//重置ar_session，保留接受队列，有些数据还要解析
void ar_session_reset(ar_session_t *ar_sess)
{
    ar_sess->max_raw_send_buf_size = 0;
    ar_sess->auto_ack_threashhold = 0;
    mbuf_reset(ar_sess->send_buf, MBUF_INIT_SIZE);
    mbuf_reset(ar_sess->send_raw_buf, MBUF_INIT_SIZE);
    mbuf_reset(ar_sess->recv_raw_buf, MBUF_INIT_SIZE);
}

static void init_packet_header(ar_header_t *header, uint64_t offset, uint64_t data_size)
{
    memset(header, 0, sizeof(header));
    //初始化ack部分的长度类型
    if (offset == 0)
    {
        header->ack_size_type = SIZE_TYPE_NONE;
    }
    else if (offset <= UCHAR_MAX)
    {
        header->ack_size_type = SIZE_TYPE_UINT8;
    }
    else if (offset <= USHRT_MAX)
    {
        header->ack_size_type = SIZE_TYPE_UINT16;
    }
    else if (offset <= UINT32_MAX)
    {
        header->ack_size_type = SIZE_TYPE_UINT32;
    }
    else
    {
        header->ack_size_type = SIZE_TYPE_UINT32;
    }

    //初始化数据部分的长度类型
    if (data_size == 0)
    {
        header->data_size_type = SIZE_TYPE_NONE;
    }
    else if (data_size <= UCHAR_MAX)
    {
        header->data_size_type = SIZE_TYPE_UINT8;
    }
    else if (data_size <= USHRT_MAX)
    {
        header->data_size_type = SIZE_TYPE_UINT16;
    }
    else if (data_size <= UINT32_MAX)
    {
        header->data_size_type = SIZE_TYPE_UINT32;
    }
    else
    {
        header->data_size_type = SIZE_TYPE_UINT64;
    }
}

void mbuf_add_number_with_type(mbuf_t *mbuf, uint64_t n)
{
    if (n <= UCHAR_MAX)
    {
        unsigned char tmp = (unsigned char)n;
        MBUF_ENQ_WITH_TYPE(mbuf, &tmp, unsigned char);
    }
    else if (n <= USHRT_MAX)
    {
        unsigned short tmp = (unsigned short)n;
        MBUF_ENQ_WITH_TYPE(mbuf, &tmp, unsigned short);
    }
    else if (n <= UINT32_MAX)
    {
        uint32_t tmp = (uint32_t)n;
        MBUF_ENQ_WITH_TYPE(mbuf, &tmp, uint32_t);
    }
    else
    {
        uint64_t tmp = (uint64_t)n;
        MBUF_ENQ_WITH_TYPE(mbuf, &tmp, uint64_t);
    }
}

//上层应用发送数据时，调用此接口对协议数据进行打包缓存，然后在从ar_session取出打包后的数据发送。
uint32_t ar_send(ar_session_t *ar_sess, const char *data, uint32_t len)
{
    if (ar_sess->send_raw_buf->data_size + len >= ar_sess->max_raw_send_buf_size)
    {
        if (ar_canlog(ar_sess))
        {
            ar_log(ar_sess, "ar_send error, send buf overflow. %s, %d/%d\n", __func__, ar_sess->send_raw_buf->data_size + len, ar_sess->max_raw_send_buf_size);
        }
        return -1;
    }

    ar_header_t header;
    init_packet_header(&header, 0, len);
    MBUF_ENQ_WITH_TYPE(ar_sess->send_buf, &header, ar_header_t);
    // mbuf_add(ar_sess->send_buf, (const char *)&header, sizeof(header));
    mbuf_add_number_with_type(ar_sess->send_buf, len);
    mbuf_add(ar_sess->send_buf, data, len);
    mbuf_add(ar_sess->send_raw_buf, data, len);
    ar_sess->total_send += len;

    return len;
}

//断线重连后，把还没有被远端确认的协议数据重新发送过去。
uint32_t ar_resend_raw(ar_session_t *ar_sess)
{
    ar_header_t header;
    uint32_t data_size = ar_sess->send_raw_buf->data_size;
    if (data_size <= 0)
    {
        return 0;
    }

    mbuf_reset(ar_sess->send_buf, MBUF_INIT_SIZE);
    const char *data = (const char *)mbuf_pullup(ar_sess->send_raw_buf);

    init_packet_header(&header, 0, data_size);
    MBUF_ENQ_WITH_TYPE(ar_sess->send_buf, &header, ar_header_t);
    mbuf_add_number_with_type(ar_sess->send_buf, data_size);
    mbuf_add(ar_sess->send_buf, data, data_size);

    return data_size;
}

//向远端发送ack，通知远端自己收到的数据offset
uint32_t ar_send_ack(ar_session_t *ar_sess)
{
    ar_header_t header;
    init_packet_header(&header, ar_sess->recv_raw_offset, 0);
    MBUF_ENQ_WITH_TYPE(ar_sess->send_buf, &header, ar_header_t);
    mbuf_add_number_with_type(ar_sess->send_buf, ar_sess->recv_raw_offset);
    ar_sess->auto_ack_recv_count = 0;
    return 0;
}

void ar_send_ack_and_raw(ar_session_t *ar_sess, const char *data, uint32_t len)
{
    ar_send_ack(ar_sess);
    ar_send(ar_sess, data, len);
}

//收到对端的ack，删除发送缓存buff数据
uint64_t ar_on_recv_ack(ar_session_t *ar_sess, uint64_t offset)
{
    if (ar_sess->remote_raw_offset == offset)
    {
        if (ar_canlog(ar_sess))
        {
            ar_log(ar_sess, "warning: local offset == remove offset. %s: offset=%ul, remote=%ul\n", __func__, ar_sess->remote_raw_offset, offset);
        }
        return 0;
    }
    else if (ar_sess->remote_raw_offset > offset)
    {
        if (ar_canlog(ar_sess))
        {
            ar_log(ar_sess, "error: local offset > remove offset. %s: offset=%ul, remote=%lu\n", __func__, ar_sess->remote_raw_offset, offset);
        }
        return -1;
    }

    //本次确认的字节数不能比缓存的多
    uint64_t delta = offset - ar_sess->send_raw_buf->data_size;
    if (delta > ar_sess->send_raw_buf->data_size)
    {
        if (ar_canlog(ar_sess))
        {
            ar_log(ar_sess, "error: send raw buf size < ack delta. %s: delta=%ul, send_raw_buf size=%ul\n", __func__, delta, ar_sess->send_raw_buf->data_size);
        }
        return -1;
    }

    ar_sess->remote_raw_offset = offset;
    mbuf_drain(ar_sess->send_raw_buf, (uint32_t)delta);
    return delta;
}

//收到对端发来的数据，放在自己的recv_raw_buf，等待上层应用接受
uint32_t ar_on_recv_data(ar_session_t *ar_sess, const char *data, uint32_t len)
{
    mbuf_add(ar_sess->recv_raw_buf, data, len);
    ar_sess->recv_raw_offset += len;

    ar_sess->auto_ack_recv_count += len;
    if (ar_sess->auto_ack_recv_count >= ar_sess->auto_ack_threashhold)
    {
        ar_send_ack(ar_sess);
    }

    return len;
}

#define READ_TYPE(p, end, dest, type)  \
    if (p + sizeof(type) - 1 > end)    \
    {                                  \
        return AR_DECODE_HEADER_ERROR; \
    }                                  \
    *dest = *((type *)(p));            \
    p += sizeof(type);                 \
    break;

//解析数据包头部信息
static int ar_parse_header(ar_header_t *header, uint32_t total_len, uint64_t *ack_offset, uint32_t *data_size, const char **pdata, uint32_t *pkg_len)
{
    const char *p = (const char *)(header + 1);             //指向header之后的第一个字节
    const char *end = (const char *)header + total_len - 1; //指向数据包的最后一个字节
    *ack_offset = 0;
    *data_size = 0;
    *pkg_len = 0;
    *pdata = NULL;

    switch (header->ack_size_type)
    {
    case SIZE_TYPE_NONE:
    {
        *ack_offset = 0;
        break;
    }
    case SIZE_TYPE_UINT8:
    {
        READ_TYPE(p, end, ack_offset, uint8_t)
    }
    case SIZE_TYPE_UINT16:
    {
        READ_TYPE(p, end, ack_offset, uint16_t)
    }
    case SIZE_TYPE_UINT32:
    {
        READ_TYPE(p, end, ack_offset, uint32_t)
    }
    case SIZE_TYPE_UINT64:
    {
        READ_TYPE(p, end, ack_offset, uint64_t)
    }

    default:
        fprintf(stderr, "invalid header->ack_size_type. %s, ack_size_type=%d\n", __func__, header->ack_size_type);
        return AR_DECODE_HEADER_ERROR;
    }

    switch (header->data_size_type)
    {
    case SIZE_TYPE_NONE:
        *data_size = 0;
        break;
    case SIZE_TYPE_UINT8:
    {
        READ_TYPE(p, end, data_size, uint8_t);
    }
    case SIZE_TYPE_UINT16:
    {
        READ_TYPE(p, end, data_size, uint16_t);
    }
    case SIZE_TYPE_UINT32:
    {
        READ_TYPE(p, end, data_size, uint32_t);
    }
    case SIZE_TYPE_UINT64:
    {
        READ_TYPE(p, end, data_size, uint32_t);
    }
    default:
        fprintf(stderr, "invalid header->data_size_type. %s, data_size_type=%d\n", __func__, header->data_size_type);
        return AR_DECODE_HEADER_ERROR;
    }

    if (*data_size > 0 && (p + *data_size - 1) > end)
    {
        return AR_DECODE_HEADER_LACK;
    }

    *pdata = p;
    *pkg_len = p - (const char *)header + *data_size;

    return AR_DECODE_HEADER_OK;
}

//传输层收到数据，输入ar_session进行解包，确认，然后再读取解包后的原始协议数据，交给上层应用处理
uint32_t ar_input(ar_session_t *ar_sess, const char *data, uint32_t data_len)
{
    ar_header_t *header = NULL;
    const char *data_frag = NULL;
    const char *pinput = NULL;
    uint64_t ack_offset = 0;
    uint32_t data_size = 0, pkg_len = 0, drain_len = 0;
    int ret = 0, use_buf = 0;

    if (ar_sess->recv_buf->data_size <= 0)
    {
        pinput = data;
    }
    else
    {
        use_buf = 1;
        mbuf_add(ar_sess->recv_buf, data, data_len);
        pinput = (const char *)mbuf_pullup(ar_sess->recv_buf);
        data_len = ar_sess->recv_buf->data_size;
    }

    while (1)
    {
        if (data_len <= sizeof(ar_header_t))
        {
            //如果剩余包的长度不足，则留待下次一起解析
            if (!use_buf)
            {
                mbuf_add(ar_sess->recv_buf, pinput, data_len);
            }

            break;
        }

        header = (ar_header_t *)pinput;
        ack_offset = data_size = pkg_len = 0;
        ret = ar_parse_header(header, data_len, &ack_offset, &data_size, &data_frag, &pkg_len);
        if (ret == AR_DECODE_HEADER_OK)
        {
            //解包成功
            if (ack_offset > 0)
            {
                //收到ack
                ar_on_recv_ack(ar_sess, ack_offset);
            }

            if (data_size > 0)
            {
                //收到数据
                ar_on_recv_data(ar_sess, data_frag, data_size);
            }

            drain_len += pkg_len;
            pinput += pkg_len;
            data_len -= pkg_len;
        }
        else if (ret == AR_DECODE_HEADER_LACK)
        {
            //包的数据不够解析，插入mbuf，等其他数据一起解析
            if (!use_buf)
            {
                mbuf_add(ar_sess->recv_buf, pinput, data_len);
            }
        }
        else
        {
            if (ar_canlog(ar_sess))
            {
                ar_log(ar_sess, "parse header error: ret=%d\n", ret);
            }

            //TODO: 这里出错了，说明数据已经不对了，考虑是否可以兼容一下，不影响后面的逻辑
            return -1;
        }
    }

    if (use_buf && drain_len > 0)
    {
        mbuf_drain(ar_sess->recv_buf, drain_len);
    }

    return 0;
}

//TODO: 提供一些操作mbuf的辅助函数
const char *ar_pull_recv_raw_buf(ar_session_t *ar_sess)
{
    return (const char *)mbuf_pullup(ar_sess->recv_raw_buf);
}

void ar_drain_recv_raw_buf(ar_session_t *ar_sess, uint32_t len)
{
    mbuf_drain(ar_sess->recv_raw_buf, len);
}

uint32_t ar_get_recv_raw_buf_length(ar_session_t *ar_sess)
{
    return ar_sess->recv_raw_buf->data_size;
}

uint64_t ar_get_recv_raw_offset(ar_session_t *ar_sess)
{
    return ar_sess->recv_raw_offset;
}


const char *ar_pullup_send_buf(ar_session_t *ar_sess)
{
    return (const char *)mbuf_pullup(ar_sess->send_buf);
}

uint32_t ar_get_send_buf_length(ar_session_t *ar_sess)
{
    return ar_sess->send_buf->data_size;
}

void ar_drain_send_buf(ar_session_t *ar_sess, uint32_t len)
{
    mbuf_drain(ar_sess->send_buf, len);
}