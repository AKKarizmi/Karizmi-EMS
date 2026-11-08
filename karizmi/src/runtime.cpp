// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/runtime.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace karizmi {
namespace {
std::string deviceName() {
    wchar_t name[128]{}; DWORD size = 128; if (!GetComputerNameW(name, &size)) return "Exam computer";
    int count = WideCharToMultiByte(CP_UTF8, 0, name, int(size), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0'); WideCharToMultiByte(CP_UTF8, 0, name, int(size), result.data(), count, nullptr, nullptr); return result.substr(0, 128);
}
struct Wipe { Secret& key; ~Wipe() { erase(key); } };
Secret tokenIdentifier(const Secret& token) { return deriveSecret(token, {}, "Karizmi reconnect credential identifier v1"); }
std::shared_ptr<SecureConnection> waitStream(Socket& listener, const Secret& ticket, const std::string& peer,
    Tick deadline, const std::atomic_bool& cancel) {
    while (now() < deadline && !cancel) {
        std::string address; auto socket = listener.accept(address, std::min(deadline, now() + 100));
        if (socket.get() == INVALID_SOCKET || address != peer) continue;
        try { return std::make_shared<SecureConnection>(std::move(socket), true, ticket, "Karizmi-stream-v1", std::min(deadline, now() + 5000)); }
        catch (...) { /* An unauthenticated attempt cannot consume the ticket. */ }
    }
    throw std::runtime_error("Encrypted screen connection timed out");
}
void expect(SecureConnection& channel, Message type, Tick deadline) {
    auto packet = channel.receive(deadline); if (!packet || packet->type != type || !packet->payload.empty()) throw std::runtime_error("Unexpected session state");
}
}
Runtime::Runtime(Role role, Engine& engine, std::shared_ptr<ClassroomResources> classroom) : role_(role), engine_(engine), session_(role), firewall_(role), classroom_(std::move(classroom)), approvals_(role) { randomBytes(instance_); label_ = deviceName(); }
Runtime::~Runtime() { shutdown(); }
void Runtime::shutdown() { if (stop_.exchange(true)) return; suspend(); if (worker_.joinable()) worker_.join(); discovery_.reset(); erase(token_); erase(pendingToken_); erase(previousToken_); }
void Runtime::status(const std::string& text) { std::lock_guard lock(mutex_); status_ = text; }
void Runtime::recoveryStage(const char* code,const std::string& text) {
    std::lock_guard lock(mutex_);status_=std::string(code)+" "+text;
    if(recoveryTrace_.size()==32)recoveryTrace_.pop_front();
    recoveryTrace_.push_back(std::to_string(now())+" "+status_);
    OutputDebugStringA(("Karizmi recovery: "+status_+"\n").c_str());
}
DisplayState Runtime::display() const {
    DisplayState result; result.session = session_.snapshot(); result.code = session_.code(now()); result.expires = session_.codeDeadline();
    std::lock_guard lock(mutex_); result.phrase = phrase_; result.status = status_;
    result.awaitingConfirmation = result.session.state == State::Confirming;
    result.recoveryTrace.assign(recoveryTrace_.begin(),recoveryTrace_.end());
    result.firewallReady = firewallReady_; return result;
}
void Runtime::startStudent() {
    if (role_ != Role::Student || worker_.joinable()) return;
    // Caller must have displayed and accepted the monitoring disclosure first.
    bootstrap_ = Socket::listen(BootstrapPort); streamListener_ = Socket::listen(StreamPort);
    auto records=approvals_.load();
    if(!records.empty()) { restored_=records.front(); instance_=restored_->local; }
    discovery_ = std::make_unique<DiscoveryResponder>(instance_, BootstrapPort, label_);
    if(restored_) { restoreApproval(*restored_); return; }
    session_.issueCode(generateCode(), now()); worker_ = std::thread([this] { studentLoop(); });
}
void Runtime::connectTeacher(const std::string& code) {
    if (role_ != Role::Teacher || !validCode(code)) throw std::runtime_error("Enter the eight-digit pairing code.");
    const auto state = session_.snapshot().state;
    if (state != State::Ready && state != State::Ended) throw std::runtime_error("Disconnect the current student before switching.");
    if (worker_.joinable()) worker_.join();
    cancel_ = false; preserveApproval_=false;
    if (!session_.beginPairing(now())) return;
    worker_ = std::thread([this, code] { try { initialTeacher(code); } catch (const std::exception& e) { endSession(e.what()); } });
}
void Runtime::connectTeacher(const Candidate& candidate, const std::string& code) {
    if (session_.snapshot().state != State::Ready && session_.snapshot().state != State::Ended)
        throw std::runtime_error("Student session already reserved");
    if (worker_.joinable()) worker_.join();
    target_ = candidate; connectTeacher(code);
}
void Runtime::confirm(bool accept) { decision_ = accept ? 1 : -1; }
void Runtime::suspend() {
    preserveApproval_=true;cancel_=true;decision_=-1;
    engine_.control(false);engine_.releaseInput();
    std::shared_ptr<SecureConnection> channel;{std::lock_guard lock(mutex_);channel=current_;}
    if(channel)channel->stop();
}
void Runtime::saveApproval() {
    if(cancel_ || stop_)return; // Explicit revocation cannot race a late re-save.
    Approval record;record.role=role_;record.local=instance_;record.peer=peer_;record.label=peerLabel_;
    record.address=peerAddress_;record.bootstrap=peerBootstrap_;record.stream=peerStream_;
    record.current=token_;record.pending=pendingToken_;record.previous=previousToken_;
    record.hasPending=havePendingToken_;record.previousDeadline=previousTokenDeadline_;record.inputAvailable=session_.snapshot().controlAvailable;
    if(!approvals_.save(record))throw std::runtime_error("Cannot protect approval for this Windows sign-in session");
}
void Runtime::restoreApproval(const Approval& record) {
    if(worker_.joinable())throw std::runtime_error("Session worker already running");
    instance_=record.local;peer_=record.peer;peerLabel_=record.label;peerAddress_=record.address;
    peerBootstrap_=record.bootstrap;peerStream_=record.stream;token_=record.current;pendingToken_=record.pending;previousToken_=record.previous;
    havePendingToken_=record.hasPending;previousTokenDeadline_=record.previousDeadline;
    session_.restoreReservation(peer_,peerLabel_,token_,record.inputAvailable,now());cancel_=false;preserveApproval_=false;
    status("Approved device reserved. Set up exam connection to resume.");
    worker_=std::thread([this]{resumeLoop();});
}
void Runtime::resumeLoop() {
    try {
        while(!stop_ && !cancel_ && !(classroom_?classroom_->ready():firewallReady_.load()))std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(!stop_ && !cancel_) {
            auto stream=role_==Role::Student?recoverStudent():recoverTeacher();
            if(stream) { {std::lock_guard lock(mutex_);current_=stream;} activeLoop(stream); }
        }
    }catch(const std::exception& error){endSession(error.what());}
    if(role_==Role::Student && !stop_)studentLoop();
}
void Runtime::disconnect() {
    preserveApproval_=false;approvals_.remove(peer_);
    controlIntent_ = false;
    revokePending_ = false;
    cancel_ = true; decision_ = -1; requestedControl_ = -1;
    engine_.releaseInput(); engine_.control(false); session_.disconnect();
    std::shared_ptr<SecureConnection> channel;
    { std::lock_guard lock(mutex_); channel = current_; }
    if (channel) { try { channel->send(Message::Disconnect); } catch (...) {} }
}
void Runtime::reconnectNow() {
    const auto state=session_.snapshot().state;
    if(role_!=Role::Teacher || (state!=State::Connected && state!=State::Recovering)) {
        status("Reconnect unavailable: pair this student first. Explicitly revoked sessions require fresh pairing.");return;
    }
    manualRetry_=true;if(classroom_)classroom_->scanNow();
    if(state==State::Recovering) {
        if(!(classroom_?classroom_->ready():firewallReady_.load()))
            recoveryStage("R011","Waiting for local setup: choose Set up exam connection and approve UAC. Student also needs setup for its current run.");
        else if(session_.snapshot().attemptRunning)
            recoveryStage("R010","Reconnect queued; an in-progress attempt will finish first.");
        else recoveryStage("R010","Reconnect queued for the next attempt. Student must be running with its approval and exam access configured.");
        return;
    }
    controlIntent_=false;revokePending_=false;requestedControl_=-1;
    engine_.control(false);engine_.releaseInput();session_.setControl(false);
    recoveryStage("R010","Repair queued: restarting this student's encrypted transport in View Only.");
    std::shared_ptr<SecureConnection> channel;{std::lock_guard lock(mutex_);channel=current_;}
    if(channel)channel->stop();
}
void Runtime::requestControl(bool enabled) {
    if (role_ != Role::Teacher || session_.snapshot().state != State::Connected) return;
    controlIntent_ = enabled;
    if (!enabled) { revokePending_ = true; engine_.control(false); engine_.releaseInput(); session_.setControl(false); }
    requestedControl_ = enabled ? 1 : 0;
}
void Runtime::frameReceived() { session_.frame(now()); }
void Runtime::setupFirewall(HWND owner, bool remove) {
    FirewallRequest request;
    if (!remove) {
        auto state = session_.snapshot().state;
        request.mode = state == State::Connected ? AccessMode::Session : (state == State::Recovering ? AccessMode::Recovery : AccessMode::Bootstrap);
        std::lock_guard lock(mutex_); strncpy_s(request.peer, peerAddress_.c_str(), _TRUNCATE);
    }
    auto result = classroom_ ? classroom_->setup(owner,remove) : firewall_.configure(owner, request);
    if (!result.error) firewallReady_ = !remove;
    int size = WideCharToMultiByte(CP_UTF8, 0, result.message, -1, nullptr, 0, nullptr, nullptr);
    std::string message(size, '\0'); WideCharToMultiByte(CP_UTF8, 0, result.message, -1, message.data(), size, nullptr, nullptr);
    if (!message.empty()) message.pop_back(); status(message);
    if (remove) disconnect();
}
bool Runtime::firewall(AccessMode mode, const std::string& peer) {
    if (!firewallReady_) { status("Use Set up exam connection before pairing."); return false; }
    FirewallRequest request; request.mode = mode; strncpy_s(request.peer, peer.c_str(), _TRUNCATE);
    auto result = firewall_.configure(nullptr, request); if (result.error) { status("Firewall update failed. Reopen setup; managed restrictions may block access."); return false; } return true;
}
void Runtime::endSession(const std::string& reason) {
    engine_.control(false); engine_.releaseInput(); engine_.detach();
    if(!preserveApproval_)approvals_.remove(peer_);
    session_.disconnect(); erase(token_); erase(pendingToken_); erase(previousToken_); previousTokenDeadline_ = 0; havePendingToken_ = false;
    { std::lock_guard lock(mutex_); if (current_) current_->stop(); current_.reset(); phrase_.clear(); peerAddress_.clear(); }
    if (firewallReady_ && !classroom_) { FirewallRequest request; request.mode = AccessMode::Remove; firewall_.configure(nullptr, request); }
    status(reason);
}
void Runtime::studentLoop() {
    while (!stop_) {
        try {
            if (cancel_.exchange(false) || session_.expire(now()) || session_.snapshot().state == State::Ended) {
                endSession("Session ended. A fresh pairing code is available."); session_.issueCode(generateCode(), now());
                if (firewallReady_) firewall(AccessMode::Bootstrap);
            }
            std::string address; auto socket = bootstrap_.accept(address, now() + 100);
            if (socket.get() == INVALID_SOCKET) continue;
            if (!firewallReady_ || !limiter_.acquire(address, now())) continue;
            try { initialStudent(std::move(socket), address); limiter_.finish(address, now(), true); }
            catch (const std::exception&) {
                limiter_.finish(address, now(), false);
                if (session_.snapshot().state != State::Ready) { endSession("Pairing ended. Use the fresh code to try again."); session_.issueCode(generateCode(), now()); if (firewallReady_) firewall(AccessMode::Bootstrap); }
            }
        } catch (const std::exception& e) { status(e.what()); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    }
    endSession("Exited");
}
void Runtime::initialStudent(Socket socket, const std::string& address) {
    const auto code = session_.code(now()); if (code.empty()) return;
    auto pair = pairAsStudent(std::move(socket), address, instance_, code, label_, std::min(session_.codeDeadline(), now() + 5000));
    if (!session_.beginPairing(now()) || !session_.reserve(pair.peer, pair.label, now())) throw std::runtime_error("Student is already reserved");
    establish(std::move(pair), true);
}
void Runtime::initialTeacher(const std::string& code) {
    if (!(classroom_ ? classroom_->ready() : firewallReady_.load())) throw std::runtime_error("Use Set up exam connection before connecting.");
    status("Searching the classroom network...");
    PairedCandidate pair;
    {
        ClassroomAttempt attempt(classroom_,cancel_);
        if (!attempt) throw std::runtime_error("Pairing cancelled");
        pair = target_ ? pairAsTeacher(*target_,instance_,code,label_,now()+5000) : findStudent(instance_, code, label_, now() + 25000, cancel_);
    }
    if (!session_.reserve(pair.peer, pair.label, now())) throw std::runtime_error("Pairing reservation expired");
    establish(std::move(pair), false);
}
bool Runtime::confirmPair(PairedCandidate& pair) {
    decision_ = 0;
    { std::lock_guard lock(mutex_); current_ = pair.channel; phrase_ = pair.phrase; peerAddress_ = pair.endpoint.address; }
    status("Compare the phrase on both screens, then both choose Confirm.");
    const auto deadline = session_.snapshot().deadline;
    if (!exchangeConfirmation(*pair.channel, decision_, cancel_, deadline)) return false;
    return session_.confirm(true, now()) && session_.confirm(false, now());
}
void Runtime::sendSecret(SecureConnection& channel, Message type, const Secret& secret) { channel.send(type, secret); }
Secret Runtime::receiveSecret(SecureConnection& channel, Message type, Tick deadline) {
    auto packet = channel.receive(deadline); if (!packet || packet->type != type || packet->payload.size() != 32) throw std::runtime_error("Invalid session credential message");
    Secret result; std::copy(packet->payload.begin(), packet->payload.end(), result.begin()); erase(packet->payload); return result;
}
void Runtime::establish(PairedCandidate pair, bool student) {
    if (!confirmPair(pair)) throw std::runtime_error("Pairing was declined, expired, or cancelled.");
    peer_ = pair.peer; peerLabel_=pair.label; peerBootstrap_ = pair.endpoint.port ? pair.endpoint.port : BootstrapPort;
    auto ticket = pair.channel->exporter("screen transport"); Wipe wipe{ticket};
    std::shared_ptr<SecureConnection> stream;
    if (student) {
        randomBytes(token_); saveApproval(); sendSecret(*pair.channel, Message::Rotate, token_);
        if (!firewall(AccessMode::Session, pair.endpoint.address)) throw std::runtime_error("Cannot restrict screen access to teacher");
        pair.channel->send(Message::StreamReady);
        stream = waitStream(streamListener_, ticket, pair.endpoint.address, std::min(session_.snapshot().deadline, now() + 10000), cancel_);
        std::array<uint8_t, 1> capabilities{uint8_t(engine_.inputAvailable())}; stream->send(Message::ControlAck, capabilities);
        expect(*stream, Message::StreamReady, now() + 5000);
    } else {
        token_ = receiveSecret(*pair.channel, Message::Rotate, now() + 5000); saveApproval(); expect(*pair.channel, Message::StreamReady, now() + 5000);
        auto socket = Socket::connect(pair.endpoint.address, peerStream_, now() + 5000);
        stream = std::make_shared<SecureConnection>(std::move(socket), false, ticket, "Karizmi-stream-v1", now() + 5000);
    }
    bool inputAllowed = engine_.inputAvailable();
    if (!student) {
        auto capabilities = stream->receive(now() + 5000);
        if (!capabilities || capabilities->type != Message::ControlAck || capabilities->payload.size() != 1 || capabilities->payload[0] > 1) throw std::runtime_error("Invalid input capability");
        inputAllowed = capabilities->payload[0] != 0; stream->send(Message::StreamReady);
    }
    if (!session_.connect(token_, inputAllowed, now())) throw std::runtime_error("Confirmation deadline expired");
    saveApproval(); pair.channel->stop();
    { std::lock_guard lock(mutex_); phrase_.clear(); current_ = stream; }
    activeLoop(std::move(stream));
}
void Runtime::activeLoop(std::shared_ptr<SecureConnection> stream) {
    for (;;) {
        if(cancel_ || stop_) {
            if(!preserveApproval_) { try { stream->send(Message::Disconnect);stream->flush(now()+1000); }catch(...){} }
            endSession("Disconnected");return;
        }
        try {
        engine_.control(false); engine_.attach(stream, peerAddress_);
        status("Connection tested. VIEW ONLY");
        while (stream->alive() && !cancel_ && !stop_) {
            int control = requestedControl_.exchange(-1);
            if (role_ == Role::Teacher && control >= 0) { std::array<uint8_t, 1> flag{uint8_t(control)}; stream->send(Message::ControlRequest, flag); }
            auto packet = stream->receive(now() + 100);
            if (!packet) continue;
            if (role_ == Role::Student && packet->type == Message::RotateAck && packet->payload.empty()) {
                session_.acknowledgeRotation(); erase(previousToken_); previousTokenDeadline_ = 0; saveApproval(); continue;
            }
            if (packet->type == Message::Disconnect && packet->payload.empty()) { cancel_ = true; break; }
            if (role_ == Role::Student && packet->type == Message::ControlRequest && packet->payload.size() == 1 && packet->payload[0] <= 1) {
                const bool enable = packet->payload[0] && engine_.inputAvailable();
                if (!enable) engine_.releaseInput(); session_.setControl(enable); engine_.control(enable);
                status(enable?"CONTROL ON":"VIEW ONLY - control disabled or unavailable");
                std::array<uint8_t,1> flag{uint8_t(enable)}; stream->send(Message::ControlAck, flag);
            } else if (role_ == Role::Teacher && packet->type == Message::ControlAck && packet->payload.size() == 1 && packet->payload[0] <= 1) {
                if (!packet->payload[0]) revokePending_ = false;
                bool enable = packet->payload[0] && controlIntent_ && session_.snapshot().controlAvailable;
                if (packet->payload[0] && !enable) stream->send(Message::ReleaseInput);
                session_.setControl(enable); engine_.control(enable);
                status(enable ? "CONTROL ON" : "VIEW ONLY — control disabled or unavailable");
            } else if (packet->type == Message::ReleaseInput && packet->payload.empty()) {
                engine_.releaseInput(); engine_.control(false); session_.setControl(false);
            } else throw std::runtime_error("Unexpected active-session message");
        }
        } catch(...) {
            // An engine/send failure can precede the transport pump noticing an
            // outage. It ends this transport, not the authenticated reservation.
            recoveryStage("R120","Active transport failed; retaining approval for authenticated recovery.");
            stream->stop();
        }
        if(stream->peerDisconnected()) { cancel_=true;preserveApproval_=false;approvals_.remove(peer_); }
        if(cancel_ && !preserveApproval_)stream->flush(now()+1000);
        engine_.control(false); engine_.releaseInput(); engine_.detach(); stream->stop();
        if (cancel_ || stop_) { endSession("Disconnected. A fresh code is required for a new pairing."); return; }
        controlIntent_ = false; revokePending_ = false; requestedControl_ = -1; session_.loseConnection(now());
        status("Interrupted: monitoring unavailable. Retrying until Disconnect or Exit. If the peer has exited, disconnect here to release the reservation.");
        recoveryStage("R020","Discovering reserved peer; monitoring unavailable.");
        stream = role_ == Role::Student ? recoverStudent() : recoverTeacher();
        if (!stream) { endSession("Recovery timed out. Monitoring ended; use a fresh student code."); return; }
        { std::lock_guard lock(mutex_); current_ = stream; }
    }
}
// Reconnection uses fresh TLS traffic keys with a short-lived random PSK, not
// TLS tickets. The new token is committed only after an authenticated exchange.
std::shared_ptr<SecureConnection> Runtime::recoverStudent() {
    auto deadline = session_.snapshot().deadline;
    Tick refreshAccess = 0;
    bool accessReady = false;
    while (now() < deadline && !cancel_ && !stop_) {
        try {
            // No adapters is normal while Wi-Fi is disconnected. Refresh both
            // subnet and local-address scopes after adapters/IPs return, without
            // discarding approval or widening screen access.
            if(now() >= refreshAccess) {
                accessReady = firewall(AccessMode::Recovery);
                refreshAccess = now() + 1000;
                if(!accessReady) recoveryStage("R105","Recovery access unavailable; waiting for network/firewall readiness. Approval retained; retrying in one second.");
            }
            if(!accessReady) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            std::string address; auto socket = bootstrap_.accept(address, std::min(deadline, now() + 100));
            if (socket.get() == INVALID_SOCKET) continue;
            auto hello = readPacket(socket, std::min(deadline, now() + 2000));
            if (hello.type != Message::ReconnectHello || hello.sequence != 0 || hello.payload.size() != 64 || !equalSecret(std::span(hello.payload).first(16), peer_) || !equalSecret(std::span(hello.payload).subspan(16,16), instance_)) continue;
            const auto claimed = std::span(hello.payload).subspan(32);
            const Secret* authentication = nullptr;
            if (equalSecret(claimed, tokenIdentifier(token_))) authentication = &token_;
            else if (havePendingToken_ && equalSecret(claimed, tokenIdentifier(pendingToken_))) authentication = &pendingToken_;
            else if (now() < previousTokenDeadline_ && equalSecret(claimed, tokenIdentifier(previousToken_))) authentication = &previousToken_;
            if (!authentication) continue;
            recoveryStage("R030","Authenticating reserved Teacher.");
            auto channel = std::make_shared<SecureConnection>(std::move(socket), true, *authentication, "Karizmi-reconnect-v1", std::min(deadline, now() + 5000));
            if (!havePendingToken_) { randomBytes(pendingToken_); havePendingToken_ = true; saveApproval(); }
            sendSecret(*channel, Message::Rotate, pendingToken_);
            expect(*channel, Message::RotateAck, std::min(deadline, now() + 5000));
            recoveryStage("R050","Updating access for authenticated address.");
            if (!firewall(AccessMode::Session, address)) {recoveryStage("R105","Firewall update failed; approval retained for retry.");refreshAccess=0;continue;}
            channel->send(Message::StreamReady);
            auto ticket = channel->exporter("screen transport"); Wipe wipe{ticket};
            recoveryStage("R060","Opening authenticated screen transport.");
            auto stream = waitStream(streamListener_, ticket, address, std::min(deadline, now() + 5000), cancel_);
            stream->send(Message::RotateCommit); expect(*stream, Message::RotateAck, std::min(deadline, now() + 5000));
            if (!session_.prepareRotation(peer_, token_, pendingToken_, now()) || !session_.finishRotation(pendingToken_, now())) throw std::runtime_error("Recovery expired");
            previousToken_ = token_; previousTokenDeadline_ = now() + ConfirmationLifetime;
            token_ = pendingToken_; erase(pendingToken_); havePendingToken_ = false;
            { std::lock_guard lock(mutex_); peerAddress_ = address; }
            recoveryStage("R080","Protecting committed credentials.");
            saveApproval(); stream->send(Message::StreamReady);
            recoveryStage("R090","Recovery authenticated; opening View Only screen.");
            return stream;
        } catch (...) {
            recoveryStage("R100","Exchange failed; restoring bootstrap access for retry.");
            if(session_.snapshot().state==State::Connected)session_.loseConnection(now());
            refreshAccess=0;accessReady=false;
        }
    }
    session_.expire(now()); return {};
}
std::shared_ptr<SecureConnection> Runtime::recoverTeacher() {
    const auto deadline = session_.snapshot().deadline;
    while (now() < deadline && !cancel_ && !stop_) {
        if (!session_.beginAttempt(now(), manualRetry_.exchange(false))) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
        try {
            ClassroomAttempt attempt(classroom_,cancel_);
            if (!attempt) return {};
            recoveryStage("R020","Checking discovered and previous endpoints.");
            auto candidates = classroom_ ? classroom_->candidates() : discover(std::min(deadline, now() + 1000), cancel_);
            candidates.insert(candidates.begin(), {peer_, peerAddress_, peerBootstrap_});
            for (const auto& candidate : candidates) {
                if (candidate.instance != peer_ || now() >= deadline || cancel_) continue;
                for (unsigned choice = 0; choice < (havePendingToken_ ? 2u : 1u); ++choice) try {
                    recoveryStage("R030","Connecting to reserved Student bootstrap endpoint.");
                    auto socket = Socket::connect(candidate.address, candidate.port, std::min(deadline, now() + 2000));
                    recoveryStage("R031","Bootstrap TCP connected; authenticating saved credentials.");
                    Writer hello; hello.bytes(instance_); hello.bytes(peer_); hello.bytes(tokenIdentifier(choice ? pendingToken_ : token_));
                    sendPacket(socket, {Message::ReconnectHello, 0, hello.data}, std::min(deadline, now() + 2000));
                    auto channel = std::make_shared<SecureConnection>(std::move(socket), false, choice ? pendingToken_ : token_, "Karizmi-reconnect-v1", std::min(deadline, now() + 5000));
                    recoveryStage("R032","Saved credential authenticated; waiting for rotation proposal.");
                    recoveryStage("R040","Protecting pending credential rotation.");
                    pendingToken_ = receiveSecret(*channel, Message::Rotate, std::min(deadline, now() + 5000)); havePendingToken_ = true; saveApproval();
                    channel->send(Message::RotateAck); expect(*channel, Message::StreamReady, std::min(deadline, now() + 5000));
                    auto ticket = channel->exporter("screen transport"); Wipe wipe{ticket};
                    recoveryStage("R060","Opening encrypted screen transport.");
                    auto screen = Socket::connect(candidate.address, peerStream_, std::min(deadline, now() + 2000));
                    auto stream = std::make_shared<SecureConnection>(std::move(screen), false, ticket, "Karizmi-stream-v1", std::min(deadline, now() + 5000));
                    recoveryStage("R070","Confirming credential rotation transaction.");
                    expect(*stream, Message::RotateCommit, std::min(deadline, now() + 5000)); stream->send(Message::RotateAck);
                    expect(*stream, Message::StreamReady, std::min(deadline, now() + 5000));
                    if (!session_.prepareRotation(peer_, token_, pendingToken_, now()) || !session_.finishRotation(pendingToken_, now())) throw std::runtime_error("Recovery expired");
                    token_ = pendingToken_; erase(pendingToken_); havePendingToken_ = false; session_.acknowledgeRotation();
                    { std::lock_guard lock(mutex_); peerAddress_ = candidate.address; }
                    recoveryStage("R080","Protecting committed approval before acknowledgement.");
                    saveApproval(); stream->send(Message::RotateAck);
                    recoveryStage("R090","Recovery authenticated; opening View Only screen.");
                    return stream;
                } catch (const SocketError& error) {
                    recoveryStage("R101","Socket failure (Winsock "+std::to_string(error.code)+"). Check Student recovery status, connectivity, and firewall access; approval retained.");
                    if(session_.snapshot().state==State::Connected)session_.loseConnection(now());
                } catch (...) {
                    recoveryStage("R103","Endpoint authentication/screen exchange failed; trying remaining endpoints or keys.");
                    if(session_.snapshot().state==State::Connected)session_.loseConnection(now());
                    /* Try the remaining authenticated endpoint/key. */
                }
            }
        } catch (...) { /* Network may be changing between Wi-Fi and Ethernet. */ }
        session_.attemptFailed(now());
        auto retry=session_.snapshot();const auto time=now();
        recoveryStage("R110","Attempt failed; next retry in "+std::to_string((retry.nextAttempt>time?retry.nextAttempt-time:0)/1000)+" seconds. Reconnect now advances it.");
    }
    session_.expire(now()); return {};
}
}
