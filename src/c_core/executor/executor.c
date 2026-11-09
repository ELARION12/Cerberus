#include "executor.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

/* Returns monotonic wall-clock time in milliseconds. Used for timeout tracking. */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * Reads all available bytes from fd into a heap-allocated buffer.
 * Uses a doubling growth strategy capped at CA_EXEC_MAX_OUTPUT.
 * Caller owns the returned buffer and must free() it.
 */
static char* read_fd_all(int fd, size_t* out_len) {
    size_t  cap  = 4096;
    size_t  len  = 0;
    char*   buf  = (char*)malloc(cap);

    if (!buf) return NULL;

    while (1) {
        if (len + 1 >= cap) {
            cap  *= 2;
            if (cap > CA_EXEC_MAX_OUTPUT) cap = CA_EXEC_MAX_OUTPUT;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n <= 0) break;  /* EOF or error (pipe closed by child) */
        len += (size_t)n;
        if (len >= CA_EXEC_MAX_OUTPUT - 1) break;
    }

    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

ca_exec_config_t ca_exec_default_config(void) {
    ca_exec_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms      = CA_EXEC_DEFAULT_TIMEOUT_MS;
    cfg.capture_stderr  = 1;
    return cfg;
}

ca_exec_result_t ca_exec_run(const ca_exec_config_t* config) {
    ca_exec_result_t result;
    memset(&result, 0, sizeof(result));
    result.status = CA_EXEC_ERROR;

    if (!config || !config->command) return result;

    int stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdout_pipe) < 0) return result;
    if (pipe(stderr_pipe) < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return result;
    }

    uint64_t start = now_ms();
    pid_t    pid   = fork();

    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        /* Child: wire both output streams to our pipes before exec. */
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        /* Close all pipe ends — child only needs the write sides, now dup'd. */
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);

        if (config->working_dir)
            chdir(config->working_dir);

        /* Build a NULL-terminated argv array from the config. */
        const char* argv[CA_EXEC_MAX_ARGS + 2];
        argv[0] = config->command;
        int argc = 1;
        int i    = 0;
        while (config->args[i] && argc < CA_EXEC_MAX_ARGS) {
            argv[argc++] = config->args[i++];
        }
        argv[argc] = NULL;

        execvp(config->command, (char* const*)argv);

        /* execvp only returns on failure. Exit with 127 (POSIX convention). */
        fprintf(stderr, "[Cerberus Executor] exec failed: %s\n",
                strerror(errno));
        _exit(127);
    }

    /* Parent: close write ends — reads would block forever otherwise. */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Set read ends non-blocking so read_fd_all() doesn't hang waiting for more data. */
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    /*
     * Poll for child exit using WNOHANG to avoid blocking the parent.
     * If the timeout is exceeded, SIGKILL the child and jump to cleanup
     * so we still collect whatever output was buffered in the pipes.
     */
    int    status     = 0;
    int    done       = 0;
    while (!done) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            done = 1;
        } else {
            uint64_t elapsed = now_ms() - start;
            if (elapsed >= config->timeout_ms) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);  /* reap to avoid zombie */
                result.status = CA_EXEC_TIMEOUT;
                fprintf(stderr,
                    "[Cerberus] Process timed out after %ums\n",
                    config->timeout_ms);
                goto cleanup;  /* still collect partial output below */
            }
            struct timespec sleep_ts = { 0, 5 * 1000000 }; // 5ms poll interval
            nanosleep(&sleep_ts, NULL);
        }
    }

    result.exit_code    = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.elapsed_ms   = (uint32_t)(now_ms() - start);
    result.status       = CA_EXEC_OK;

cleanup:
    result.stdout_data = read_fd_all(stdout_pipe[0], &result.stdout_len);
    result.stderr_data = config->capture_stderr
                         ? read_fd_all(stderr_pipe[0], &result.stderr_len)
                         : NULL;

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    return result;
}

ca_exec_result_t ca_exec_shell(const char* cmd, uint32_t timeout_ms) {
    ca_exec_config_t config = ca_exec_default_config();
    config.command          = "/bin/sh";
    config.args[0]          = "-c";
    config.args[1]          = cmd;
    config.args[2]          = NULL;
    config.timeout_ms       = timeout_ms ? timeout_ms
                                         : CA_EXEC_DEFAULT_TIMEOUT_MS;
    return ca_exec_run(&config);
}

void ca_exec_result_free(ca_exec_result_t* result) {
    if (!result) return;
    free(result->stdout_data);
    free(result->stderr_data);
    result->stdout_data = NULL;
    result->stderr_data = NULL;
}