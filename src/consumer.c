#include "consumer.h"
#include "common.h"
#include <cjson/cJSON.h>
#include <stdlib.h>
#include <string.h>

void *consumer_thread(void *arg)
{
    (void)arg;

    for (;;) {
        char *msg;
        size_t len;

        if (cb_pop(&g_cb, &msg, &len) != 0) {
            break;
        }
        (void)len;

        cJSON *root = cJSON_Parse(msg);
        if (root) {
            const cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
            if (cJSON_IsString(kind) && kind->valuestring) {
                pthread_mutex_lock(&g_counters.mutex);
                if (strcmp(kind->valuestring, "commit") == 0) {
                    g_counters.commit_count++;
                } else if (strcmp(kind->valuestring, "identity") == 0) {
                    g_counters.identity_count++;
                } else if (strcmp(kind->valuestring, "account") == 0) {
                    g_counters.account_count++;
                } else if (strcmp(kind->valuestring, "info") == 0) {
                    g_counters.info_count++;
                }
                pthread_mutex_unlock(&g_counters.mutex);
            }
            cJSON_Delete(root);
        }

        free(msg);
    }

    return NULL;
}
