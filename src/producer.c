#include "producer.h"
#include "common.h"
#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define JETSTREAM_HOST "jetstream1.us-east.bsky.network"
#define JETSTREAM_PORT 443
#define JETSTREAM_PATH "/subscribe?wantedCollections=app.bsky.feed.post"

#define BACKOFF_INITIAL_SEC 1
#define BACKOFF_MAX_SEC     30

static char  *accum_buf = NULL;
static size_t accum_len = 0;
static size_t accum_cap = 0;

static volatile sig_atomic_t conn_alive = 0;
static volatile sig_atomic_t ever_established = 0;

static void accum_reset(void)
{
    free(accum_buf);
    accum_buf = NULL;
    accum_len = 0;
    accum_cap = 0;
}

static int accum_append(const void *data, size_t len)
{
    if (accum_len + len > accum_cap) {
        size_t newcap = (accum_cap == 0) ? 4096 : accum_cap * 2;
        while (newcap < accum_len + len) {
            newcap *= 2;
        }
        char *nb = (char *)realloc(accum_buf, newcap);
        if (!nb) {
            return -1;
        }
        accum_buf = nb;
        accum_cap = newcap;
    }
    memcpy(accum_buf + accum_len, data, len);
    accum_len += len;
    return 0;
}

static int callback_jetstream(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len)
{
    (void)user;

    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        fprintf(stderr, "[Producer] Connected to Jetstream.\n");
        conn_alive = 1;
        ever_established = 1;
        accum_reset();
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (accum_append(in, len) != 0) {
            fprintf(stderr, "[Producer] Out of memory reassembling frame, dropping it.\n");
            accum_reset();
            break;
        }
        if (lws_is_final_fragment(wsi)) {
            cb_push(&g_cb, accum_buf, accum_len);
            accum_reset();
        }
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        fprintf(stderr, "[Producer] Connection error: %s\n",
                (in && len) ? (char *)in : "(no details)");
        conn_alive = 0;
        accum_reset();
        return -1;

    case LWS_CALLBACK_CLIENT_CLOSED:
        fprintf(stderr, "[Producer] Connection closed.\n");
        conn_alive = 0;
        accum_reset();
        return -1;

    default:
        break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {
        .name                  = "jetstream-protocol",
        .callback              = callback_jetstream,
        .per_session_data_size = 0,
        .rx_buffer_size        = 65536,
    },
    { 0 }
};

void *producer_thread(void *arg)
{
    (void)arg;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "[Producer] lws_create_context failed.\n");
        return NULL;
    }

    int backoff_sec = BACKOFF_INITIAL_SEC;

    while (g_running) {
        struct lws_client_connect_info ccinfo;
        memset(&ccinfo, 0, sizeof(ccinfo));
        ccinfo.context  = context;
        ccinfo.address  = JETSTREAM_HOST;
        ccinfo.port     = JETSTREAM_PORT;
        ccinfo.path     = JETSTREAM_PATH;
        ccinfo.host     = JETSTREAM_HOST;
        ccinfo.origin   = JETSTREAM_HOST;
        ccinfo.protocol = protocols[0].name;
        ccinfo.ssl_connection = LCCSCF_USE_SSL;

        conn_alive = 1;
        ever_established = 0;
        struct lws *wsi = lws_client_connect_via_info(&ccinfo);

        if (!wsi) {
            fprintf(stderr, "[Producer] Connect attempt failed, retrying in %d s\n",
                    backoff_sec);
            sleep((unsigned int)backoff_sec);
            backoff_sec = (backoff_sec * 2 < BACKOFF_MAX_SEC) ? backoff_sec * 2 : BACKOFF_MAX_SEC;
            continue;
        }

        while (g_running && conn_alive) {
            lws_service(context, 100 /* ms */);
        }

        if (ever_established) {
            backoff_sec = BACKOFF_INITIAL_SEC;
            atomic_fetch_add(&g_reconnect_count, 1);
        }

        if (g_running) {
            fprintf(stderr, "[Producer] Reconnecting in %d s...\n", backoff_sec);
            sleep((unsigned int)backoff_sec);
            if (!ever_established) {
                backoff_sec = (backoff_sec * 2 < BACKOFF_MAX_SEC) ? backoff_sec * 2 : BACKOFF_MAX_SEC;
            }
        }
    }

    accum_reset();
    lws_context_destroy(context);
    fprintf(stderr, "[Producer] Thread exiting.\n");
    return NULL;
}
