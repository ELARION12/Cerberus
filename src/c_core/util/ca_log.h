#pragma once

/*
 * Progress logging for the engine. Off by default, so importing and using the
 * library prints nothing. Turn it on with CERBERUS_VERBOSE=1 or
 * ca_set_log_enabled(1). Errors go to stderr and are not affected.
 */

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int  ca_log_enabled(void);
void ca_set_log_enabled(int on);

#ifdef __cplusplus
}
#endif

#define CA_LOG(...) do { if (ca_log_enabled()) printf(__VA_ARGS__); } while (0)
