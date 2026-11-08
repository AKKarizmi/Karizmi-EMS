// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/security.h"
#include <iostream>
#include <stdexcept>
using namespace karizmi;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Failed: " #x); } while (0)
template<class F> void rejects(F f) { bool caught = false; try { f(); } catch (...) { caught = true; } CHECK(caught); }
static void tlsTest(const Secret& clientKey, const Secret& serverKey, bool success) {
    std::deque<std::vector<uint8_t>> toServer, toClient;
    std::vector<uint8_t> received;
    TlsChannel server(true, serverKey, "pairing-v1", [&](auto b) { toClient.emplace_back(b.begin(), b.end()); },
        [&](auto b) { received.insert(received.end(), b.begin(), b.end()); });
    TlsChannel client(false, clientKey, "pairing-v1", [&](auto b) { toServer.emplace_back(b.begin(), b.end()); }, [](auto) {});
    auto pump = [&] {
        for (int i = 0; i < 50 && (!toServer.empty() || !toClient.empty()); ++i) {
            while (!toServer.empty()) { auto b = std::move(toServer.front()); toServer.pop_front(); server.receive(b); }
            while (!toClient.empty()) { auto b = std::move(toClient.front()); toClient.pop_front(); client.receive(b); }
        }
    };
    if (!success) { rejects(pump); CHECK(received.empty()); return; }
    pump(); CHECK(client.active()); CHECK(server.active());
    CHECK(equalSecret(client.exporter("confirmation"), server.exporter("confirmation")));
    std::vector<uint8_t> plain{1,2,3,4,5}; client.send(plain); CHECK(!toServer.empty());
    CHECK(toServer.front() != plain); pump(); CHECK(received == plain);
    client.send(plain); auto damaged = toServer.front(); toServer.clear(); damaged.back() ^= 1;
    rejects([&] { server.receive(damaged); });
}
int main() try {
    for (int i = 0; i < 1000; ++i) CHECK(validCode(generateCode()));
    InstanceId student{}, teacher{}; randomBytes(student); randomBytes(teacher);
    PairServer server("00001234", student, teacher);
    auto client = pairClient("00001234", student, teacher, server.challenge());
    auto key = server.finish(client.response); CHECK(equalSecret(key, client.key));
    rejects([&] { server.finish(client.response); }); tlsTest(client.key, key, true);
    PairServer wrong("00001234", student, teacher); auto c = pairClient("00001235", student, teacher, wrong.challenge());
    auto k = wrong.finish(c.response); CHECK(!equalSecret(k, c.key)); tlsTest(c.key, k, false);
    PairServer altered("00001234", student, teacher); auto other = teacher; other[0] ^= 1;
    auto a = pairClient("00001234", student, other, altered.challenge()); auto ak = altered.finish(a.response);
    CHECK(!equalSecret(a.key, ak)); tlsTest(a.key, ak, false);
    PairServer invalid("00001234", student, teacher); std::vector<uint8_t> zero(384);
    rejects([&] { invalid.finish(zero); });
    auto challenge = invalid.challenge(); std::fill(challenge.begin() + 32, challenge.end(), 0);
    rejects([&] { pairClient("00001234", student, teacher, challenge); });
    std::cout << "Security tests passed: SRP agreement, wrong codes, identity binding, TLS encryption and tamper rejection\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
