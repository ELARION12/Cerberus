#include "ca_log.h"

#include <stdatomic.h>
#include <stdlib.h>

/* -1 = not decided yet, read the environment on first use */
static _Atomic int g_log_enabled = -1;

int ca_log_enabled(void) {
    int v = atomic_load_explicit(&g_log_enabled, memory_order_relaxed);
    if (v < 0) {
        const char* env = getenv("CERBERUS_VERBOSE");
        v = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
        atomic_store_explicit(&g_log_enabled, v, memory_order_relaxed);
    }
    return v;
}

void ca_set_log_enabled(int on) {
    atomic_store_explicit(&g_log_enabled, on ? 1 : 0, memory_order_relaxed);
}
