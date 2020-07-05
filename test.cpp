//test ar_seesion
#include "ar_session.h"

#include <errno.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

typedef struct {
    mbuf_t *c2s_buf;
    mbuf_t *s2c_buf;
} DataPipeline ;

static const char *pto_data = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const int pto_data_len = strlen(pto_data);

void client(DataPipeline *pipeline, ar_session_t *ar_sess)
{
    const char *input = (const char *)mbuf_pullup(pipeline->s2c_buf);
    uint32_t len = pipeline->s2c_buf->data_size;
    ar_input(ar_sess, input, len);
    mbuf_drain(pipeline->s2c_buf, len);

    ar_send(ar_sess, pto_data, pto_data_len);
    const char *pack_data = ar_pullup_send_buf(ar_sess);
    mbuf_add(pipeline->c2s_buf, pack_data, ar_get_send_buf_length(ar_sess));
    ar_drain_send_buf(ar_sess, ar_get_send_buf_length(ar_sess));
}

void server(DataPipeline *pipeline, ar_session_t *ar_sess)
{
    const char *input = (const char *)mbuf_pullup(pipeline->c2s_buf);
    uint32_t len = pipeline->c2s_buf->data_size;
    ar_input(ar_sess, input, len);
    mbuf_drain(pipeline->c2s_buf, len);

    ar_send(ar_sess, pto_data, pto_data_len);
    const char *pack_data = ar_pullup_send_buf(ar_sess);
    mbuf_add(pipeline->s2c_buf, pack_data, ar_get_send_buf_length(ar_sess));
    ar_drain_send_buf(ar_sess, ar_get_send_buf_length(ar_sess));
}

int main()
{
    DataPipeline *pipeline = (DataPipeline *)malloc(sizeof(DataPipeline));
    pipeline->c2s_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    pipeline->s2c_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    mbuf_init(pipeline->c2s_buf, 1024);
    mbuf_init(pipeline->s2c_buf, 1024);

    ar_session_t *ar_client = ar_session_new(1, 10 * 1024 * 1024, 20);
    ar_session_t *ar_server = ar_session_new(2, 10 * 1024 * 1024, 20);

    int state = 1;
    while (1) {
        if (state == 1) {
            client(pipeline, ar_client);
            // printf("client recv raw data: %lu, remote_offset=%lu, total_send=%lu\n", ar_client->recv_raw_offset, ar_client->remote_raw_offset, ar_client->total_send);
            state = 2;
        } else {
            server(pipeline, ar_server);
            // printf("server recv raw data: %lu, remote_offset=%lu,total_send=%d\n", ar_server->recv_raw_offset, ar_server->remote_raw_offset, ar_server->total_send);
            state = 1;
        }


        if (ar_get_recv_raw_offset(ar_client) >= 100 * 1024) {
            printf("server recv raw data: %lu, remote_offset=%lu,total_send=%d\n", ar_server->recv_raw_offset, ar_server->remote_raw_offset, ar_server->total_send);

            printf("client recv raw data: %lu, remote_offset=%lu, total_send=%lu\n", ar_client->recv_raw_offset, ar_client->remote_raw_offset, ar_client->total_send);
            break;
        }
    }
    
    return 0;
}


