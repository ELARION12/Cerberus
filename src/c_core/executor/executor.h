#ifndef CA_EXECUTOR_H
#define CA_EXECUTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum bytes captured from stdout/stderr (1 MB). */
#define CA_EXEC_MAX_OUTPUT      (1024 * 1024)
/* Maximum number of command-line arguments. */
#define CA_EXEC_MAX_ARGS        64
/* Default per-process timeout in milliseconds. */
#define CA_EXEC_DEFAULT_TIMEOUT_MS  5000

typedef enum {
    CA_EXEC_OK       = 0,   /* Process exited normally. */
    CA_EXEC_ERROR    = -1,  /* Setup error (fork/pipe failed). */
    CA_EXEC_TIMEOUT  = -2,  /* Process killed due to timeout. */
    CA_EXEC_OOM      = -3,  /* Memory allocation failed. */
} ca_exec_status_t;

typedef struct {
    const char* command;                    /* Executable to run (argv[0]). */
    const char* args[CA_EXEC_MAX_ARGS];     /* NULL-terminated argument list. */
    const char* env_vars[64];               /* NULL-terminated env overrides (unused currently). */
    const char* working_dir;               /* Working directory, or NULL for inherited. */
    const char* stdin_data;                /* Data piped to stdin (unused currently). */
    uint32_t    timeout_ms;                /* Kill timeout in milliseconds. */
    int         capture_stderr;            /* Non-zero to capture stderr into result. */
} ca_exec_config_t;

typedef struct {
    char*            stdout_data;  /* Heap-allocated stdout output (caller must free via ca_exec_result_free). */
    char*            stderr_data;  /* Heap-allocated stderr output, or NULL if not captured. */
    size_t           stdout_len;
    size_t           stderr_len;
    int              exit_code;    /* Exit code, or -1 if process was signalled. */
    uint32_t         elapsed_ms;   /* Wall-clock time from fork to exit. */
    ca_exec_status_t status;
} ca_exec_result_t;

ca_exec_result_t ca_exec_run(const ca_exec_config_t* config);
ca_exec_result_t ca_exec_shell(const char* cmd, uint32_t timeout_ms);
void             ca_exec_result_free(ca_exec_result_t* result);
ca_exec_config_t ca_exec_default_config(void);

#ifdef __cplusplus
}
#endif

#endif /* CA_EXECUTOR_H */