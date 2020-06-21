#ifndef AR_SESSION_H_
#define AR_SESSION_H_

/*
ar_session(async rpc session)，主要是为了解决断线重连协议数据的丢失问题。主要的实现思路是，ar_session会将发送的协议数据缓存，然后cs通过ack进行确认。当网络太差，导致断线重连，双端可以选择将对方未收到的数据重新发送一次，可以保证数据非重复发送和可靠。。这个逻辑在引擎处理，对逻辑层是透明的，逻辑层是感受不到数据丢失的。
ar_session工作在传输层和应用层之间。数据流如下:
    app---->protocol data ------>ar_session(ar_send)-------->tcp/udp
                                                                |
    app<---protocol data <--ar_session(ar_input)<--tcp/udp<-----|

*/

#include "mbuf.h"
#include <stdint.h>

const int SIZE_TYPE_NONE = 0;
const int SIZE_TYPE_UINT8 = 1;
const int SIZE_TYPE_UINT16 = 2;
const int SIZE_TYPE_UINT32 = 3;
const int SIZE_TYPE_UINT64 = 4; //实际应用中，包的大小很小，不会这么大，只有ack_offset会用到这么大

const int AR_DECODE_HEADER_OK = 0;
const int AR_DECODE_HEADER_ERROR = -1;
const int AR_DECODE_HEADER_LACK = -2;

typedef struct ar_header_s
{
    unsigned char ack_size_type : 4;  //ack数据长度的类型
    unsigned char data_size_type : 4; //data数据长度的类型
} ar_header_t;

typedef struct ar_session_s
{
    int session_id; //暂时没有用到，但是整合到引擎中肯定会用到
    void *ud;       //用户数据

    uint64_t recv_raw_offset;       //收到的协议原始数据的字节数
    uint64_t remote_raw_offset;     //远端确认收到的字节数
    uint32_t max_raw_send_buf_size; // 本端缓存的协议数据最大字节数，还没有被远端ack的部分

    uint32_t auto_ack_threashhold; //每接收这些字节就自动发送一个ack
    uint32_t auto_ack_recv_count;  //距离上次ack，到目前接收了多少数据

    mbuf_t *send_buf;     //发送buf，保存被ar_session打包的数据
    mbuf_t *send_raw_buf; //协议数据发送buf，保存协议原始数据

    mbuf_t *recv_buf;     //接收buf，保存远端的被ar_session打包的数据
    mbuf_t *recv_raw_buf; //收到到的原始协议数据

    int canlog;
    void (*write_log)(const char *data, ar_session_t *ar_sess, void *ud);

} ar_session_t;

#if defined(__cplusplus)
extern "C"
{
#endif

    void ar_session_debug(ar_session_t *ar_sess);

    ar_session_t *ar_session_new(uint32_t session_id, uint32_t max_raw_send_buf_size, uint32_t auto_ack_threashhold);

    void ar_session_free(ar_session_t *ar_sess);

    void ar_session_reset(ar_session_t *ar_sess);

    uint32_t ar_send(ar_session_t *ar_sess, const char *data, uint32_t len);

    uint32_t ar_resend_raw(ar_session_t *ar_sess);

    uint32_t ar_send_ack(ar_session_t *ar_sess);

    void ar_send_ack_and_raw(ar_session_t *ar_session, const char *data, uint32_t len);

    uint64_t ar_on_recv_ack(ar_session_t *ar_sess, uint64_t offset);
    uint32_t ar_on_recv_data(ar_session_t *ar_sess, const char *data, uint32_t len);

    uint32_t ar_input(ar_session_t *ar_sess, const char *data, uint32_t len);

    //TODO: buf相关操作，考虑改用ringbuf，去掉内存的分配和释放消耗
    //接收buf
    const char *ar_pull_recv_raw_buf(ar_session_t *ar_sess);

#if defined(__cplusplus)
}
#endif

static char __check_header_size[sizeof(ar_header_t) == 1 ? 1 : -1];
#endif //AR_SESSION_H_