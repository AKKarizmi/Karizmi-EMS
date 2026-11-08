// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/core.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace karizmi {
void erase(std::span<uint8_t> bytes) noexcept {
    volatile uint8_t* p = bytes.data();
    for (size_t i = 0; i < bytes.size(); ++i) p[i] = 0;
}
bool equalSecret(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept {
    if (a.size() != b.size()) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < a.size(); ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}
bool validCode(const std::string& code) noexcept {
    return code.size() == 8 && std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::optional<uint32_t> parseAddress(const std::string& text) noexcept {
    uint32_t result = 0; size_t offset = 0;
    for (unsigned part = 0; part < 4; ++part) {
        unsigned value = 0, digits = 0;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
            if (++digits > 3) return {};
            value = value * 10 + unsigned(text[offset++] - '0');
        }
        if (!digits || value > 255) return {};
        result = (result << 8) | value;
        if (part < 3 && (offset >= text.size() || text[offset++] != '.')) return {};
    }
    if (offset != text.size() || (result >> 24) == 0 || (result >> 24) == 127 || (result >> 24) >= 224) return {};
    return result;
}
Session::~Session() { disconnect(); }
bool Session::issueCode(const std::string& code, Tick now) {
    std::lock_guard lock(mutex_);
    if (role_ != Role::Student || !validCode(code) || (value_.state != State::Ready && value_.state != State::Ended)) return false;
    endLocked(); value_.state = State::Ready; code_ = code; codeDeadline_ = now + CodeLifetime;
    return true;
}
std::string Session::code(Tick now) const {
    std::lock_guard lock(mutex_);
    return now < codeDeadline_ ? code_ : std::string{};
}
Tick Session::codeDeadline() const { std::lock_guard lock(mutex_); return codeDeadline_; }
bool Session::beginPairing(Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Ready && value_.state != State::Ended) return false;
    if (role_ == Role::Student && (code_.empty() || now >= codeDeadline_)) return false;
    value_.state = State::Pairing;
    value_.deadline = role_ == Role::Student ? codeDeadline_ : now + CodeLifetime;
    return true;
}
bool Session::reserve(const InstanceId& peer, const std::string& label, Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Pairing || now >= value_.deadline || label.size() > 128) return false;
    peer_ = peer; value_.peer = label; value_.state = State::Confirming;
    value_.deadline = std::min(value_.deadline, now + ConfirmationLifetime);
    localConfirmed_ = remoteConfirmed_ = false;
    return true;
}
bool Session::confirm(bool local, Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Confirming || now >= value_.deadline) return false;
    (local ? localConfirmed_ : remoteConfirmed_) = true;
    return true;
}
bool Session::connect(const Secret& token, bool inputAllowed, Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Confirming || now >= value_.deadline || !localConfirmed_ || !remoteConfirmed_) return false;
    token_ = token; value_.state = State::Connected; value_.control = false;
    value_.controlAvailable = inputAllowed; value_.deadline = 0; ++value_.generation;
    erase(std::span(reinterpret_cast<uint8_t*>(code_.data()), code_.size())); code_.clear(); codeDeadline_ = 0;
    return true;
}
bool Session::setControl(bool enable) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Connected || (enable && !value_.controlAvailable)) return false;
    value_.control = enable; return true;
}
bool Session::loseConnection(Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Connected) return false;
    value_.control = false; value_.state = State::Recovering;
    value_.deadline = RecoveryDeadline; value_.nextAttempt = now;
    value_.attemptRunning = false; retry_ = 0;
    return true;
}
void Session::restoreReservation(const InstanceId& peer,const std::string& label,const Secret& token,bool inputAllowed,Tick now) {
    std::lock_guard lock(mutex_);endLocked();peer_=peer;token_=token;value_.peer=label;
    value_.state=State::Recovering;value_.controlAvailable=inputAllowed;value_.deadline=RecoveryDeadline;value_.nextAttempt=now;retry_=0;
}
bool Session::beginAttempt(Tick now, bool manual) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Recovering || now >= value_.deadline || value_.attemptRunning || (!manual && now < value_.nextAttempt)) return false;
    value_.attemptRunning = true; return true;
}
void Session::attemptFailed(Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Recovering) return;
    value_.attemptRunning = false;
    const Tick delay = std::min<Tick>(1000ull << std::min(retry_++, 4u), 10000);
    value_.nextAttempt = std::min(now + delay, value_.deadline);
}
bool Session::authenticates(const InstanceId& peer, const Secret& token, Tick now) const {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Recovering || now >= value_.deadline || peer_ != peer) return false;
    return equalSecret(token_, token) || (rotationPending_ && equalSecret(pending_, token)) ||
           (previousValid_ && now < previousDeadline_ && equalSecret(previous_, token));
}
bool Session::prepareRotation(const InstanceId& peer, const Secret& oldToken, const Secret& replacement, Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Recovering || now >= value_.deadline || peer != peer_ ||
        !(equalSecret(oldToken, token_) || (rotationPending_ && equalSecret(oldToken, pending_)) ||
        (previousValid_ && now < previousDeadline_ && equalSecret(oldToken, previous_)))) return false;
    // Replayed parallel attempts cannot replace a pending rotation.
    if (rotationPending_) return equalSecret(replacement, pending_);
    pending_ = replacement; rotationPending_ = true; return true;
}
bool Session::finishRotation(const Secret& replacement, Tick now) {
    std::lock_guard lock(mutex_);
    if (value_.state != State::Recovering || now >= value_.deadline || !rotationPending_ || !equalSecret(pending_, replacement)) return false;
    previous_ = token_; previousValid_ = true; previousDeadline_ = value_.deadline;
    token_ = pending_; erase(pending_); rotationPending_ = false;
    value_.state = State::Connected; value_.control = false; value_.attemptRunning = false;
    value_.deadline = 0; ++value_.generation; return true;
}
void Session::acknowledgeRotation() {
    std::lock_guard lock(mutex_); erase(previous_); previousValid_ = false; previousDeadline_ = 0;
}
void Session::frame(Tick now) { std::lock_guard lock(mutex_); if (value_.state == State::Connected) value_.lastFrame = now; }
bool Session::expire(Tick now) {
    std::lock_guard lock(mutex_);
    if ((value_.deadline && now >= value_.deadline) || (value_.state == State::Ready && codeDeadline_ && now >= codeDeadline_)) {
        endLocked(); return true;
    }
    if (previousValid_ && now >= previousDeadline_) { erase(previous_); previousValid_ = false; }
    return false;
}
void Session::endLocked() {
    erase(token_); erase(pending_); erase(previous_); erase(peer_);
    erase(std::span(reinterpret_cast<uint8_t*>(code_.data()), code_.size())); code_.clear(); codeDeadline_ = 0;
    localConfirmed_ = remoteConfirmed_ = rotationPending_ = previousValid_ = false;
    value_.state = State::Ended; value_.control = value_.controlAvailable = value_.attemptRunning = false;
    value_.peer.clear(); value_.deadline = value_.nextAttempt = 0; ++value_.generation;
}
void Session::disconnect() { std::lock_guard lock(mutex_); endLocked(); }
Snapshot Session::snapshot() const { std::lock_guard lock(mutex_); return value_; }
void AttemptLimiter::prune(Bucket& b, Tick now) {
    while (!b.failures.empty() && now >= b.failures.front() && now - b.failures.front() >= 60000) b.failures.pop_front();
}
bool AttemptLimiter::acquire(const std::string& address, Tick now) {
    std::lock_guard lock(mutex_); prune(global_, now);
    // Bound source-address memory under a spoofing/flood attack.
    for (auto it = addresses_.begin(); it != addresses_.end();) {
        prune(it->second, now);
        if (!it->second.active && it->second.failures.empty() && now >= it->second.cooldown) it = addresses_.erase(it); else ++it;
    }
    if (addresses_.size() >= 1024 && !addresses_.count(address)) return false;
    auto& b = addresses_[address];
    if (global_.active >= 4 || b.active || now < b.cooldown || now < global_.cooldown || b.failures.size() >= 5 || global_.failures.size() >= 20) return false;
    ++global_.active; ++b.active; return true;
}
void AttemptLimiter::finish(const std::string& address, Tick now, bool success) {
    std::lock_guard lock(mutex_); auto it = addresses_.find(address);
    if (it == addresses_.end() || !it->second.active) return;
    auto& b = it->second; --b.active; --global_.active;
    if (!success) {
        prune(b, now); prune(global_, now); b.failures.push_back(now); global_.failures.push_back(now);
        if (b.failures.size() >= 5) b.cooldown = now + 60000;
        if (global_.failures.size() >= 20) global_.cooldown = now + 60000;
    }
}
void Writer::u16(uint16_t n) { data.push_back(uint8_t(n >> 8)); data.push_back(uint8_t(n)); }
void Writer::u32(uint32_t n) { u16(uint16_t(n >> 16)); u16(uint16_t(n)); }
void Writer::u64(uint64_t n) { u32(uint32_t(n >> 32)); u32(uint32_t(n)); }
void Writer::bytes(std::span<const uint8_t> b) { data.insert(data.end(), b.begin(), b.end()); }
void Writer::text(const std::string& text, size_t max) {
    if (text.size() > max || text.size() > 65535 || text.find('\0') != std::string::npos) throw std::runtime_error("Invalid text length");
    u16(uint16_t(text.size())); bytes({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}
std::vector<uint8_t> Reader::bytes(size_t size) {
    if (size > data_.size() - offset_) throw std::runtime_error("Truncated protocol message");
    auto b = data_.subspan(offset_, size); offset_ += size; return {b.begin(), b.end()};
}
uint16_t Reader::u16() { auto b = bytes(2); return uint16_t((b[0] << 8) | b[1]); }
uint32_t Reader::u32() { auto hi = u16(); return (uint32_t(hi) << 16) | u16(); }
uint64_t Reader::u64() { auto hi = u32(); return (uint64_t(hi) << 32) | u32(); }
std::string Reader::text(size_t max) {
    auto size = u16(); if (size > max) throw std::runtime_error("Oversized text"); auto b = bytes(size);
    if (std::find(b.begin(), b.end(), 0) != b.end()) throw std::runtime_error("Embedded NUL");
    return {b.begin(), b.end()};
}
void Reader::end() const { if (offset_ != data_.size()) throw std::runtime_error("Trailing protocol data"); }
uint32_t payloadLength(std::span<const uint8_t> header, size_t maxPayload) {
    if (header.size() != HeaderSize) throw std::runtime_error("Invalid header");
    Reader r(header);
    if (r.u32() != 0x4b41525a || r.u16() != ProtocolVersion) throw std::runtime_error("Incompatible Karizmi protocol. Update Teacher and Student together.");
    const auto type = r.u16(); if (type < 1 || type > uint16_t(Message::Rfb)) throw std::runtime_error("Unknown message");
    const auto size = r.u32(); r.u64(); r.end();
    if (size > maxPayload) throw std::runtime_error("Oversized protocol message"); return size;
}
std::vector<uint8_t> encode(const Packet& p) {
    if (p.payload.size() > MaxPayload) throw std::runtime_error("Oversized protocol message");
    Writer w; w.u32(0x4b41525a); w.u16(ProtocolVersion); w.u16(uint16_t(p.type)); w.u32(uint32_t(p.payload.size())); w.u64(p.sequence); w.bytes(p.payload); return w.data;
}
Packet decode(std::span<const uint8_t> bytes, size_t maxPayload) {
    if (bytes.size() < HeaderSize) throw std::runtime_error("Truncated header");
    const auto size = payloadLength(bytes.first(HeaderSize), maxPayload);
    if (bytes.size() != HeaderSize + size) throw std::runtime_error("Invalid packet length");
    Reader r(bytes); r.u32(); r.u16(); Packet p; p.type = Message(r.u16()); r.u32(); p.sequence = r.u64(); p.payload = r.bytes(size); r.end(); return p;
}
uint64_t Sequence::next() { if (next_ == std::numeric_limits<uint64_t>::max()) throw std::runtime_error("Sequence exhausted"); return next_++; }
bool Sequence::accept(uint64_t n) { if (n != next_ || next_ == std::numeric_limits<uint64_t>::max()) return false; ++next_; return true; }
}
