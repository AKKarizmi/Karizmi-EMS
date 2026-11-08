// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "network.h"
#include <windows.h>

namespace karizmi {
enum class AccessMode : uint32_t { Remove = 0, Bootstrap = 1, Session = 2, Recovery = 3 };
struct FirewallRequest {
    uint32_t version = 1;
    AccessMode mode = AccessMode::Remove;
    uint32_t discovery = DiscoveryPort, bootstrap = BootstrapPort, stream = StreamPort;
    char peer[16]{};
};
struct FirewallResult { uint32_t error = 0; wchar_t message[2048]{}; };
bool validateFirewallRequest(const FirewallRequest& request);
// Returns nullopt unless the exact helper command was supplied. Helper has no
// general-purpose shell/process/registry operation in its IPC interface.
std::optional<int> firewallHelperMain(Role role);
class FirewallBroker {
public:
    explicit FirewallBroker(Role role) : role_(role) {}
    ~FirewallBroker();
    FirewallResult configure(HWND owner, const FirewallRequest& request);
    bool running() const { return pipe_ != INVALID_HANDLE_VALUE; }
private:
    void start(HWND owner);
    Role role_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE, process_ = nullptr;
    std::mutex mutex_;
};
}
