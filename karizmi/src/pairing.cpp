// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/pairing.h"
#include <algorithm>
#include <future>
#include <stdexcept>

namespace karizmi {
namespace {
void require(const Packet& packet, Message type, uint64_t sequence) {
    if (packet.type != type || packet.sequence != sequence) throw std::runtime_error("Unexpected pairing message");
}
InstanceId readId(Reader& r) { auto b = r.bytes(16); InstanceId id{}; std::copy(b.begin(), b.end(), id.begin()); return id; }
std::string safeLabel(const std::string& label) {
    if (label.empty() || label.size() > 128 || std::any_of(label.begin(), label.end(), [](unsigned char c) { return c < 32 || c == 127; })) throw std::runtime_error("Invalid device label");
    // UTF-8 validation also rejects overlong sequences and invalid codepoints.
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label.data(), int(label.size()), nullptr, 0)) throw std::runtime_error("Invalid device label encoding");
    return label;
}
std::string readIdentity(SecureConnection& channel, const InstanceId& expected, Tick deadline) {
    auto packet = channel.receive(deadline);
    if (!packet || packet->type != Message::Identity) throw std::runtime_error("Peer identity was not authenticated");
    Reader r(packet->payload); auto id = readId(r); auto label = safeLabel(r.text()); r.end();
    if (id != expected) throw std::runtime_error("Peer identity mismatch"); return label;
}
void sendIdentity(SecureConnection& channel, const InstanceId& id, const std::string& label) {
    Writer w; w.bytes(id); w.text(safeLabel(label)); channel.send(Message::Identity, w.data);
}
struct Wipe { Secret& key; ~Wipe() { erase(key); } };
}
PairedCandidate pairAsTeacher(const Candidate& candidate, const InstanceId& teacher,
    const std::string& code, const std::string& label, Tick deadline) {
    auto socket = Socket::connect(candidate.address, candidate.port, deadline);
    Writer hello; hello.bytes(teacher); hello.bytes(candidate.instance);
    sendPacket(socket, {Message::PairHello, 0, hello.data}, deadline);
    auto challenge = readPacket(socket, deadline); require(challenge, Message::PairChallenge, 1);
    auto answer = pairClient(code, candidate.instance, teacher, challenge.payload); Wipe wipe{answer.key};
    sendPacket(socket, {Message::PairResponse, 2, answer.response}, deadline);
    auto channel = std::make_shared<SecureConnection>(std::move(socket), false, answer.key, "Karizmi-pair-v1", deadline);
    sendIdentity(*channel, teacher, label);
    auto peerLabel = readIdentity(*channel, candidate.instance, deadline);
    auto confirmation = channel->exporter("confirmation"); Wipe wipeConfirmation{confirmation};
    return {candidate, candidate.instance, peerLabel, confirmationPhrase(confirmation), channel};
}
PairedCandidate pairAsStudent(Socket socket, const std::string& address, const InstanceId& student,
    const std::string& code, const std::string& label, Tick deadline) {
    auto hello = readPacket(socket, deadline); require(hello, Message::PairHello, 0);
    Reader r(hello.payload); auto teacher = readId(r); auto target = readId(r); r.end();
    if (target != student) throw std::runtime_error("Discovery endpoint changed; rediscover the student");
    PairServer exchange(code, student, teacher);
    sendPacket(socket, {Message::PairChallenge, 1, exchange.challenge()}, deadline);
    auto answer = readPacket(socket, deadline); require(answer, Message::PairResponse, 2);
    auto key = exchange.finish(answer.payload); Wipe wipe{key};
    auto channel = std::make_shared<SecureConnection>(std::move(socket), true, key, "Karizmi-pair-v1", deadline);
    auto peerLabel = readIdentity(*channel, teacher, deadline); sendIdentity(*channel, student, label);
    auto confirmation = channel->exporter("confirmation"); Wipe wipeConfirmation{confirmation};
    return {{teacher, address, 0}, teacher, peerLabel, confirmationPhrase(confirmation), channel};
}
PairedCandidate findStudent(const InstanceId& teacher, const std::string& code, const std::string& label,
    Tick deadline, const std::atomic_bool& cancel) {
    if (!validCode(code)) throw std::runtime_error("Enter exactly eight digits, including leading zeros.");
    auto candidates = discover(std::min(deadline, now() + 1500), cancel);
    if (candidates.empty()) throw std::runtime_error("No student found. Check firewall setup and the same classroom Wi-Fi. Wi-Fi client isolation can prevent discovery and connections.");
    std::vector<PairedCandidate> matches;
    // Bounded parallel probing. Each candidate consumes a normal rate-limited
    // PAKE attempt, never an offline password test or a broadcast code hash.
    for (size_t base = 0; base < candidates.size() && now() < deadline && !cancel; base += 4) {
        std::vector<std::future<std::optional<PairedCandidate>>> pending;
        for (size_t i = base; i < std::min(base + 4, candidates.size()); ++i) {
            auto candidate = candidates[i];
            pending.push_back(std::async(std::launch::async, [=]() -> std::optional<PairedCandidate> {
                try { return pairAsTeacher(candidate, teacher, code, label, std::min(deadline, now() + 5000)); } catch (...) { return {}; }
            }));
        }
        for (auto& attempt : pending) {
            auto match = attempt.get();
            if (match && std::none_of(matches.begin(), matches.end(), [&](const auto& m) { return m.peer == match->peer; })) matches.push_back(std::move(*match));
        }
        if (matches.size() > 1) {
            for (auto& match : matches) match.channel->stop();
            throw std::runtime_error("More than one student authenticated with that code. Ask the intended student to generate a fresh code.");
        }
        if (base + 4 < candidates.size() && now() >= deadline) throw std::runtime_error("Discovery checks did not finish. Retry pairing; no student was selected.");
    }
    if (cancel) throw std::runtime_error("Pairing cancelled");
    if (matches.size() != 1) throw std::runtime_error("Code incorrect, expired, rate limited, or peer unreachable. Check the student screen and retry.");
    return std::move(matches.front());
}
bool exchangeConfirmation(SecureConnection& channel, const std::atomic_int& localDecision,
    const std::atomic_bool& cancel, Tick deadline) {
    bool sent = false, accepted = false;
    while (channel.alive() && now() < deadline && !cancel) {
        auto decision = localDecision.load();
        if (decision < 0) { channel.send(Message::Reject); return false; }
        if (decision > 0 && !sent) { channel.send(Message::Confirm); sent = true; }
        // The peer may advance immediately after our confirmation. Once both
        // decisions are known, leave subsequent session messages for establish().
        if (sent && accepted) return true;
        auto message = channel.receive(std::min(deadline, now() + 100));
        if (message) {
            if (!message->payload.empty()) return false;
            if (message->type == Message::Confirm && !accepted) accepted = true;
            else return false;
        }
        if (sent && accepted) return true;
    }
    return false;
}
}
