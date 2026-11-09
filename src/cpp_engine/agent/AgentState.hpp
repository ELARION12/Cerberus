#pragma once

#include <string>

namespace geryon {

enum class AgentState {
    IDLE,
    THINKING,
    PLANNING,
    ACTING,
    REFLECTING,
    DONE,
    ERROR
};

inline std::string agentStateToString(AgentState state) {
    switch (state) {
        case AgentState::IDLE:       return "IDLE";
        case AgentState::THINKING:   return "THINKING";
        case AgentState::PLANNING:   return "PLANNING";
        case AgentState::ACTING:     return "ACTING";
        case AgentState::REFLECTING: return "REFLECTING";
        case AgentState::DONE:       return "DONE";
        case AgentState::ERROR:      return "ERROR";
        default:                     return "UNKNOWN";
    }
}

} // namespace geryon