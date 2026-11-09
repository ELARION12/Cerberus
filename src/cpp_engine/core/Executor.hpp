#pragma once

#include "executor.h"
#include <string>
#include <cstdint>

namespace cerberus {

struct ExecResult {
    std::string      stdout_data;
    std::string      stderr_data;
    int              exit_code  = 0;
    uint32_t         elapsed_ms = 0;
    ca_exec_status_t status     = CA_EXEC_ERROR;

    bool ok()      const { return status == CA_EXEC_OK && exit_code == 0; }
    bool timeout() const { return status == CA_EXEC_TIMEOUT;              }
    bool failed()  const { return !ok();                                  }
};

class Executor {
public:
    static ExecResult shell(const std::string& cmd,
                            uint32_t timeout_ms = 5000) {
        ca_exec_result_t raw = ca_exec_shell(cmd.c_str(), timeout_ms);

        ExecResult result;
        result.stdout_data = raw.stdout_data ? raw.stdout_data : "";
        result.stderr_data = raw.stderr_data ? raw.stderr_data : "";
        result.exit_code   = raw.exit_code;
        result.elapsed_ms  = raw.elapsed_ms;
        result.status      = raw.status;

        ca_exec_result_free(&raw);
        return result;
    }
};

} // namespace cerberus