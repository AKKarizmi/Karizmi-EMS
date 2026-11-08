// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "network.h"

namespace karizmi {
struct PairedCandidate {
    Candidate endpoint;
    InstanceId peer{};
    std::string label, phrase;
    std::shared_ptr<SecureConnection> channel;
};
// The returned channel is authenticated but is NOT authorized for monitoring.
// The caller must complete both confirmations and acquire an exclusive Session.
PairedCandidate pairAsTeacher(const Candidate& candidate, const InstanceId& teacher,
                             const std::string& code, const std::string& label, Tick deadline);
PairedCandidate pairAsStudent(Socket socket, const std::string& address, const InstanceId& student,
                             const std::string& code, const std::string& label, Tick deadline);
// Search all discovered candidates before selecting. Duplicate interfaces for the
// same instance are collapsed; multiple authenticated instances are an error.
PairedCandidate findStudent(const InstanceId& teacher, const std::string& code,
                           const std::string& label, Tick deadline, const std::atomic_bool& cancel);
bool exchangeConfirmation(SecureConnection& channel, const std::atomic_int& localDecision,
                          const std::atomic_bool& cancel, Tick deadline);
}
