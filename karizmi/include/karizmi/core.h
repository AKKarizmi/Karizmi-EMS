// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace karizmi {
using Tick = uint64_t; // monotonic milliseconds, never wall-clock time
using Secret = std::array<uint8_t, 32>;
using InstanceId = std::array<uint8_t, 16>;
constexpr uint16_t ProtocolVersion = 3;
constexpr Tick CodeLifetime = 300000, ConfirmationLifetime = 30000;
constexpr Tick RecoveryDeadline = UINT64_MAX, HeartbeatInterval = 2000, HeartbeatTimeout = 6000;
constexpr uint16_t DiscoveryPort = 45900, BootstrapPort = 45901, StreamPort = 45902;
void erase(std::span<uint8_t> bytes) noexcept;
bool equalSecret(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept;
bool validCode(const std::string& code) noexcept;
std::optional<uint32_t> parseAddress(const std::string& address) noexcept;

enum class Role : uint8_t { Teacher = 1, Student = 2 };
enum class State : uint8_t { Ready, Pairing, Confirming, Connected, Recovering, Ended };
struct Snapshot {
    State state = State::Ready;
    bool control = false, controlAvailable = false, attemptRunning = false;
    Tick deadline = 0, lastFrame = 0, nextAttempt = 0;
    uint64_t generation = 0;
    std::string peer;
};

// All ownership/control transitions are serialized here, not in UI callbacks.
class Session {
public:
    explicit Session(Role role) : role_(role) {}
    ~Session();
    bool issueCode(const std::string& code, Tick now);
    std::string code(Tick now) const;
    Tick codeDeadline() const;
    bool beginPairing(Tick now);
    bool reserve(const InstanceId& peer, const std::string& label, Tick now);
    bool confirm(bool local, Tick now);
    bool connect(const Secret& reconnect, bool inputAllowed, Tick now);
    bool setControl(bool enable);
    bool loseConnection(Tick now);
    void restoreReservation(const InstanceId& peer,const std::string& label,const Secret& token,bool inputAllowed,Tick now);
    bool beginAttempt(Tick now, bool manual = false);
    void attemptFailed(Tick now);
    bool authenticates(const InstanceId& peer, const Secret& token, Tick now) const;
    bool prepareRotation(const InstanceId& peer, const Secret& oldToken,
                         const Secret& replacement, Tick now);
    bool finishRotation(const Secret& replacement, Tick now);
    void acknowledgeRotation();
    void frame(Tick now);
    bool expire(Tick now);
    void disconnect();
    Snapshot snapshot() const;
private:
    void endLocked();
    mutable std::mutex mutex_;
    Role role_;
    Snapshot value_;
    std::string code_;
    Tick codeDeadline_ = 0;
    InstanceId peer_{};
    Secret token_{}, pending_{}, previous_{};
    bool localConfirmed_ = false, remoteConfirmed_ = false, rotationPending_ = false;
    bool previousValid_ = false;
    Tick previousDeadline_ = 0;
    unsigned retry_ = 0;
};

class AttemptLimiter {
public:
    bool acquire(const std::string& address, Tick now);
    void finish(const std::string& address, Tick now, bool success);
private:
    struct Bucket { std::deque<Tick> failures; Tick cooldown = 0; unsigned active = 0; };
    static void prune(Bucket& bucket, Tick now);
    std::mutex mutex_;
    std::map<std::string, Bucket> addresses_;
    Bucket global_;
};

enum class Message : uint16_t {
    DiscoveryQuery = 1, DiscoveryReply, PairHello, PairChallenge, PairResponse,
    Identity, Confirm, Reject, StreamTicket, StreamHello, StreamReady,
    Heartbeat, Disconnect, ControlRequest, ControlAck, ReleaseInput,
    ReconnectHello, Rotate, RotateAck, RotateCommit, Rfb
};
struct Packet { Message type; uint64_t sequence; std::vector<uint8_t> payload; };
constexpr size_t HeaderSize = 20, MaxPayload = 65536, MaxBootstrapPayload = 4096;
std::vector<uint8_t> encode(const Packet& packet);
Packet decode(std::span<const uint8_t> bytes, size_t maxPayload = MaxPayload);
uint32_t payloadLength(std::span<const uint8_t> header, size_t maxPayload = MaxPayload);
class Sequence {
public:
    uint64_t next();
    bool accept(uint64_t value);
private:
    uint64_t next_ = 0;
};
// Explicit bounds and byte ordering; no packed native structs on the wire.
class Writer {
public:
    void u16(uint16_t n); void u32(uint32_t n); void u64(uint64_t n);
    void bytes(std::span<const uint8_t> b);
    void text(const std::string& text, size_t max = 128);
    std::vector<uint8_t> data;
};
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}
    uint16_t u16(); uint32_t u32(); uint64_t u64();
    std::vector<uint8_t> bytes(size_t size);
    std::string text(size_t max = 128);
    void end() const;
private:
    std::span<const uint8_t> data_; size_t offset_ = 0;
};
}
