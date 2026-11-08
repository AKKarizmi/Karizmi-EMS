// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "pairing.h"
#include "firewall.h"
#include "classroom.h"
#include "approvals.h"

namespace karizmi {
struct Engine {
    virtual ~Engine() = default;
    virtual bool inputAvailable() const = 0;
    virtual void attach(std::shared_ptr<SecureConnection> channel, const std::string& peer) = 0;
    virtual void detach() = 0;
    virtual void control(bool enabled) = 0;
    virtual void releaseInput() = 0;
};
struct DisplayState {
    Snapshot session;
    std::string code, phrase, status;
    Tick expires = 0;
    bool awaitingConfirmation = false, firewallReady = false;
    std::vector<std::string> recoveryTrace;
};
// Network operations run off the window thread. Engine implementations marshal
// window work to their UI thread; no UI callback holds a session mutex.
class Runtime {
public:
    Runtime(Role role, Engine& engine, std::shared_ptr<ClassroomResources> classroom = {});
    ~Runtime();
    void shutdown();
    void suspend();
    void restoreApproval(const Approval& record);
    void startStudent();
    void connectTeacher(const std::string& code);
    void connectTeacher(const Candidate& candidate, const std::string& code);
    void confirm(bool accept);
    void disconnect();
    void reconnectNow();
    void requestControl(bool enabled);
    bool controlRevocationPending() const { return revokePending_; }
    void frameReceived();
    void setupFirewall(HWND owner, bool remove);
    DisplayState display() const;
private:
    friend struct RuntimeRecoveryTest;
    void studentLoop();
    void initialStudent(Socket socket, const std::string& address);
    void initialTeacher(const std::string& code);
    void establish(PairedCandidate pair, bool student);
    bool confirmPair(PairedCandidate& pair);
    void activeLoop(std::shared_ptr<SecureConnection> stream);
    std::shared_ptr<SecureConnection> recoverStudent();
    std::shared_ptr<SecureConnection> recoverTeacher();
    void endSession(const std::string& reason);
    void status(const std::string& text);
    bool firewall(AccessMode mode, const std::string& peer = {});
    void sendSecret(SecureConnection& channel, Message type, const Secret& secret);
    Secret receiveSecret(SecureConnection& channel, Message type, Tick deadline);
    Role role_; Engine& engine_; Network network_; Session session_;
    InstanceId instance_{}, peer_{};
    Secret token_{}, pendingToken_{}, previousToken_{};
    Tick previousTokenDeadline_ = 0;
    bool havePendingToken_ = false;
    std::string label_, peerAddress_;
    uint16_t peerBootstrap_ = BootstrapPort, peerStream_ = StreamPort;
    mutable std::mutex mutex_;
    std::string phrase_, status_ = "Ready";
    std::atomic_bool stop_ = false, cancel_ = false, manualRetry_ = false, firewallReady_ = false;
    std::atomic_int decision_ = 0, requestedControl_ = -1;
    std::atomic_bool controlIntent_ = false;
    std::atomic_bool revokePending_ = false;
    std::shared_ptr<SecureConnection> current_;
    std::thread worker_;
    FirewallBroker firewall_;
    Socket bootstrap_, streamListener_;
    std::unique_ptr<DiscoveryResponder> discovery_;
    AttemptLimiter limiter_;
    std::shared_ptr<ClassroomResources> classroom_;
    std::optional<Candidate> target_;
    ApprovalStore approvals_;
    std::atomic_bool preserveApproval_=false;
    std::string peerLabel_;
    std::optional<Approval> restored_;
    void saveApproval();
    void resumeLoop();
    void recoveryStage(const char* code,const std::string& text);
    std::deque<std::string> recoveryTrace_;
};
}
