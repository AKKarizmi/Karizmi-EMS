// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/security.h"
#include <botan/auto_rng.h>
#include <botan/system_rng.h>
#include <botan/srp6.h>
#include <botan/hash.h>
#include <botan/kdf.h>
#include <botan/credentials_manager.h>
#include <botan/tls_client.h>
#include <botan/tls_server.h>
#include <botan/tls_callbacks.h>
#include <botan/tls_policy.h>
#include <botan/tls_session_manager_noop.h>
#include <botan/tls_session.h>
#include <botan/tls_alert.h>
#include <botan/tls_external_psk.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace karizmi {
namespace {
constexpr const char* Group = "modp/srp/3072";
constexpr const char* Hash = "SHA-256";
std::vector<uint8_t> context(const InstanceId& student, const InstanceId& teacher) {
    Writer w; w.text("Karizmi PAKE teacher->student"); w.u16(ProtocolVersion); w.bytes(teacher); w.bytes(student); return w.data;
}
Botan::BigInt publicValue(std::span<const uint8_t> bytes) {
    if (bytes.size() != 384) throw std::runtime_error("Invalid SRP public length");
    auto n = Botan::BigInt::from_bytes(bytes);
    const Botan::DL_Group group(Group);
    if (n <= 0 || n >= group.get_p()) throw std::runtime_error("Invalid SRP public value");
    return n;
}
}
void randomBytes(std::span<uint8_t> output) { Botan::system_rng().randomize(output); }
std::string generateCode() {
    // Rejection sampling prevents modulo bias across the 100 million codes.
    uint32_t n; std::array<uint8_t, 4> bytes;
    do { randomBytes(bytes); n = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3]; }
    while (n >= 4200000000u);
    std::ostringstream out; out << std::setw(8) << std::setfill('0') << n % 100000000;
    return out.str();
}
Secret deriveSecret(std::span<const uint8_t> secret, std::span<const uint8_t> transcript, const std::string& purpose) {
    auto hash = Botan::HashFunction::create_or_throw(Hash); hash->update(transcript);
    auto salt = hash->final();
    auto kdf = Botan::KDF::create_or_throw("HKDF(SHA-256)");
    Secret out; kdf->derive_key(out, secret, salt, {reinterpret_cast<const uint8_t*>(purpose.data()), purpose.size()}); return out;
}
std::string confirmationPhrase(const Secret& secret) {
    static constexpr const char* words[] = {"amber", "birch", "cedar", "dawn", "elm", "fern", "gold", "hill", "iris", "jade", "kite", "lake", "moon", "nest", "oak", "pine"};
    auto derived = deriveSecret(secret, {}, "Karizmi confirmation phrase v1");
    std::string result;
    for (size_t i = 0; i < 6; ++i) { if (i) result += " "; result += words[(derived[i / 2] >> ((i % 2) * 4)) & 15]; }
    erase(derived); return result;
}
struct PairServer::Impl {
    Botan::AutoSeeded_RNG rng;
    Botan::SRP6_Server_Session srp;
    std::vector<uint8_t> transcript, challenge;
    bool used = false;
};
PairServer::PairServer(const std::string& code, const InstanceId& student, const InstanceId& teacher) : impl_(std::make_unique<Impl>()) {
    if (!validCode(code)) throw std::runtime_error("Invalid pairing code");
    std::vector<uint8_t> salt(32); randomBytes(salt);
    auto verifier = Botan::srp6_generate_verifier("Karizmi-v1", code, salt, Group, Hash);
    auto b = impl_->srp.step1(verifier, Group, Hash, impl_->rng);
    Writer w; w.bytes(salt); w.bytes(b.serialize<std::vector<uint8_t>>(384));
    impl_->challenge = w.data; impl_->transcript = context(student, teacher);
    impl_->transcript.insert(impl_->transcript.end(), w.data.begin(), w.data.end());
}
PairServer::~PairServer() = default;
std::vector<uint8_t> PairServer::challenge() const { return impl_->challenge; }
Secret PairServer::finish(std::span<const uint8_t> response) {
    if (impl_->used) throw std::runtime_error("SRP exchange already consumed"); impl_->used = true;
    auto a = publicValue(response); auto shared = impl_->srp.step2(a).bits_of();
    impl_->transcript.insert(impl_->transcript.end(), response.begin(), response.end());
    return deriveSecret(shared, impl_->transcript, "Karizmi pairing TLS 1.3 external PSK v1");
}
PairClientResult pairClient(const std::string& code, const InstanceId& student, const InstanceId& teacher, std::span<const uint8_t> challenge) {
    if (!validCode(code) || challenge.size() != 416) throw std::runtime_error("Invalid SRP challenge");
    Botan::AutoSeeded_RNG rng;
    auto b = publicValue(challenge.subspan(32)); std::vector<uint8_t> salt(challenge.begin(), challenge.begin() + 32);
    auto [a, shared] = Botan::srp6_client_agree("Karizmi-v1", code, Group, Hash, salt, b, rng);
    PairClientResult result; result.response = a.serialize<std::vector<uint8_t>>(384);
    auto transcript = context(student, teacher); transcript.insert(transcript.end(), challenge.begin(), challenge.end());
    transcript.insert(transcript.end(), result.response.begin(), result.response.end());
    result.key = deriveSecret(shared.bits_of(), transcript, "Karizmi pairing TLS 1.3 external PSK v1"); return result;
}
namespace {
class Credentials final : public Botan::Credentials_Manager {
public:
    Credentials(const Secret& key, std::string identity) : key_(key.begin(), key.end()), identity_(std::move(identity)) {}
    std::vector<Botan::TLS::ExternalPSK> find_preshared_keys(std::string_view, Botan::TLS::Connection_Side,
        const std::vector<std::string>& ids, const std::optional<std::string>& prf) override {
        if (prf && *prf != Hash) return {};
        if (!ids.empty() && std::find(ids.begin(), ids.end(), identity_) == ids.end()) return {};
        std::vector<Botan::TLS::ExternalPSK> out; out.emplace_back(identity_, Hash, key_); return out;
    }
private:
    Botan::secure_vector<uint8_t> key_; std::string identity_;
};
class Policy final : public Botan::TLS::Policy {
public:
    bool allow_tls12() const override { return false; }
    bool allow_tls13() const override { return true; }
    bool allow_dtls12() const override { return false; }
    bool allow_ssl_key_log_file() const override { return false; }
    std::vector<std::string> allowed_ciphers() const override { return {"AES-128/GCM"}; }
    std::vector<Botan::TLS::Group_Params> key_exchange_groups() const override { return {Botan::TLS::Group_Params::X25519}; }
    std::vector<Botan::TLS::Group_Params> key_exchange_groups_to_offer() const override { return key_exchange_groups(); }
    size_t new_session_tickets_upon_handshake_success() const override { return 0; }
    bool tls_13_middlebox_compatibility_mode() const override { return false; }
};
class Callbacks final : public Botan::TLS::Callbacks {
public:
    TlsChannel::Sink wire, plain;
    void tls_emit_data(std::span<const uint8_t> bytes) override { wire(bytes); }
    void tls_record_received(uint64_t, std::span<const uint8_t> bytes) override { plain(bytes); }
    void tls_alert(Botan::TLS::Alert alert) override {
        if (alert.is_fatal()) throw std::runtime_error("TLS authentication or transport failed");
    }
    bool tls_should_persist_resumption_information(const Botan::TLS::Session&) override { return false; }
    void tls_session_established(const Botan::TLS::Session_Summary& session) override {
        if (session.version() != Botan::TLS::Protocol_Version::TLS_V13) throw std::runtime_error("TLS 1.3 required");
    }
};
}
struct TlsChannel::Impl {
    std::shared_ptr<Callbacks> callbacks;
    std::unique_ptr<Botan::TLS::Channel> channel;
    std::string identity;
};
TlsChannel::TlsChannel(bool server, const Secret& key, const std::string& identity, Sink wire, Sink plain) : impl_(std::make_unique<Impl>()) {
    impl_->identity = identity; impl_->callbacks = std::make_shared<Callbacks>();
    impl_->callbacks->wire = std::move(wire); impl_->callbacks->plain = std::move(plain);
    auto credentials = std::make_shared<Credentials>(key, identity);
    auto sessions = std::make_shared<Botan::TLS::Session_Manager_Noop>();
    auto policy = std::make_shared<Policy>(); auto rng = std::make_shared<Botan::AutoSeeded_RNG>();
    if (server) impl_->channel = std::make_unique<Botan::TLS::Server>(impl_->callbacks, sessions, credentials, policy, rng);
    else impl_->channel = std::make_unique<Botan::TLS::Client>(impl_->callbacks, sessions, credentials, policy, rng,
        Botan::TLS::Server_Information(), Botan::TLS::Protocol_Version::TLS_V13);
}
TlsChannel::~TlsChannel() = default;
void TlsChannel::receive(std::span<const uint8_t> bytes) {
    impl_->channel->received_data(bytes);
    if (impl_->channel->is_active() && impl_->channel->external_psk_identity() != std::optional<std::string>(impl_->identity)) throw std::runtime_error("Authenticated PSK required");
}
void TlsChannel::send(std::span<const uint8_t> bytes) {
    if (!active()) throw std::runtime_error("Encrypted channel not authenticated"); impl_->channel->send(bytes);
}
bool TlsChannel::active() const {
    return impl_->channel->is_active() && impl_->channel->external_psk_identity() == std::optional<std::string>(impl_->identity);
}
Secret TlsChannel::exporter(const std::string& purpose) const {
    if (!active()) throw std::runtime_error("Encrypted channel not authenticated");
    auto key = impl_->channel->key_material_export("Karizmi v1 " + purpose, "", 32).bits_of();
    Secret result; std::copy(key.begin(), key.end(), result.begin()); return result;
}
void TlsChannel::close() { impl_->channel->close(); }
}
