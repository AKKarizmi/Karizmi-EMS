// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "network.h"
namespace karizmi {
struct Approval {
    Role role = Role::Student;
    InstanceId local{}, peer{};
    std::string label,address;
    uint16_t bootstrap=BootstrapPort,stream=StreamPort;
    Secret current{},pending{},previous{};
    bool hasPending=false,inputAvailable=false;
    Tick previousDeadline=0;
    ~Approval() { erase(current); erase(pending); erase(previous); }
};
// User/logon-scoped volatile registry, DPAPI-protected. No durable enrollment.
class ApprovalStore {
public:
    explicit ApprovalStore(Role role,std::wstring testNamespace=L"");
    ~ApprovalStore();
    std::vector<Approval> load() const;
    bool save(const Approval& record);
    void remove(const InstanceId& peer);
    void clear();
private:
    Role role_; HKEY key_=nullptr; std::array<uint8_t,8> logon_{};
};
}
