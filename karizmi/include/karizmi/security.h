// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core.h"
#include <memory>
#include <functional>

namespace karizmi {
void randomBytes(std::span<uint8_t> output);
std::string generateCode();
Secret deriveSecret(std::span<const uint8_t> secret, std::span<const uint8_t> transcript, const std::string& purpose);
std::string confirmationPhrase(const Secret& secret);

// One-use SRP server exchange. Fixed suite; peer cannot choose weaker parameters.
class PairServer {
public:
    PairServer(const std::string& code, const InstanceId& student, const InstanceId& teacher);
    ~PairServer();
    std::vector<uint8_t> challenge() const;
    Secret finish(std::span<const uint8_t> response);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
struct PairClientResult { std::vector<uint8_t> response; Secret key; };
PairClientResult pairClient(const std::string& code, const InstanceId& student,
                            const InstanceId& teacher, std::span<const uint8_t> challenge);

// Serialized by its owner. Callbacks must enqueue rather than re-enter this object.
class TlsChannel {
public:
    using Sink = std::function<void(std::span<const uint8_t>)>;
    TlsChannel(bool server, const Secret& key, const std::string& identity, Sink ciphertext, Sink plaintext);
    ~TlsChannel();
    void receive(std::span<const uint8_t> ciphertext);
    void send(std::span<const uint8_t> plaintext);
    bool active() const;
    Secret exporter(const std::string& purpose) const;
    void close();
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
