// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/core.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace karizmi;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Failed: " #x); } while (0)
template<class F> void rejects(F f) { bool caught = false; try { f(); } catch (...) { caught = true; } CHECK(caught); }
static void connected(Session& s, Tick now = 0) {
    InstanceId peer{}; peer[0] = 1; Secret token{}; token[0] = 7;
    CHECK(s.issueCode("00001234", now)); CHECK(s.beginPairing(now));
    CHECK(s.reserve(peer, "Teacher", now)); CHECK(s.confirm(true, now));
    CHECK(!s.connect(token, true, now)); CHECK(s.confirm(false, now)); CHECK(s.connect(token, true, now));
    CHECK(s.code(now).empty()); CHECK(!s.snapshot().control);
}
int main() try {
    CHECK(validCode("00001234")); CHECK(!validCode("1234567")); CHECK(!validCode("1234567x"));
    CHECK(parseAddress("192.168.1.2")); CHECK(!parseAddress("192.168.1.2/24"));
    CHECK(!parseAddress("127.0.0.1")); CHECK(!parseAddress("224.0.0.1")); CHECK(!parseAddress("1.2.3.256"));
    Session s(Role::Student); CHECK(s.issueCode("00001234", 100)); CHECK(s.code(300099) == "00001234");
    CHECK(s.code(300100).empty()); CHECK(!s.beginPairing(300100)); CHECK(s.expire(300100));
    connected(s, 400000); CHECK(s.setControl(true)); CHECK(s.loseConnection(400100));
    CHECK(!s.snapshot().control); CHECK(!s.beginPairing(400101)); CHECK(!s.issueCode("87654321", 400101));
    const auto deadline = s.snapshot().deadline; CHECK(deadline == RecoveryDeadline);
    CHECK(s.beginAttempt(400100)); CHECK(!s.beginAttempt(400101, true)); s.attemptFailed(400200);
    CHECK(!s.beginAttempt(400201)); CHECK(s.beginAttempt(400201, true)); CHECK(s.snapshot().deadline == deadline);
    s.attemptFailed(400202); CHECK(s.beginAttempt(400000 + 86400000, true)); CHECK(!s.expire(400000 + 86400000));
    s.disconnect();
    CHECK(s.snapshot().state == State::Ended);
    connected(s); CHECK(s.loseConnection(10)); InstanceId peer{}; peer[0] = 1; Secret token{}; token[0] = 7;
    Secret next{}; next[0] = 8; CHECK(s.authenticates(peer, token, 11));
    CHECK(s.prepareRotation(peer, token, next, 11)); CHECK(!s.prepareRotation(peer, token, token, 11));
    CHECK(s.finishRotation(next, 12)); CHECK(!s.finishRotation(next, 13)); CHECK(!s.authenticates(peer, token, 13));
    CHECK(!s.snapshot().control); s.acknowledgeRotation(); CHECK(s.loseConnection(20));
    CHECK(!s.authenticates(peer, token, 21)); CHECK(s.authenticates(peer, next, 21)); s.disconnect();
    CHECK(!s.authenticates(peer, next, 22));
    s.restoreReservation(peer,"Restored teacher",next,true,1000);
    CHECK(s.snapshot().state==State::Recovering && !s.snapshot().control);
    CHECK(s.authenticates(peer,next,1001));CHECK(!s.beginPairing(1001));
    CHECK(s.code(1001).empty());s.disconnect();CHECK(!s.authenticates(peer,next,1002));
    Session race(Role::Student); CHECK(race.issueCode("12345678", 0)); CHECK(race.beginPairing(0));
    std::atomic<int> winners = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 20; ++i) threads.emplace_back([&, i] { InstanceId p{}; p[0] = uint8_t(i); if (race.reserve(p, "teacher", 1)) ++winners; });
    for (auto& t : threads) t.join(); CHECK(winners == 1);
    CHECK(race.expire(30001));
    AttemptLimiter limit;
    for (int i = 0; i < 5; ++i) { CHECK(limit.acquire("192.168.1.2", i)); limit.finish("192.168.1.2", i, false); }
    CHECK(!limit.acquire("192.168.1.2", 1000)); CHECK(limit.acquire("192.168.1.2", 60004)); limit.finish("192.168.1.2", 60004, true);
    for (int i = 0; i < 4; ++i) CHECK(limit.acquire(std::to_string(i), 70000));
    CHECK(!limit.acquire("fifth", 70000));
    Packet p{Message::Identity, 8, {1,2,3}}; auto wire = encode(p); auto decoded = decode(wire);
    CHECK(decoded.sequence == 8 && decoded.payload == p.payload);
    wire[4] = 99; rejects([&] { decode(wire); }); wire = encode(p); wire.pop_back(); rejects([&] { decode(wire); });
    wire = encode(p); wire[8] = 1; rejects([&] { decode(wire); });
    Sequence seq; CHECK(seq.accept(0)); CHECK(!seq.accept(0)); CHECK(!seq.accept(2)); CHECK(seq.accept(1));
    Writer w; w.text("teacher"); w.u64(0xfedcba9876543210); Reader r(w.data); CHECK(r.text() == "teacher"); CHECK(r.u64() == 0xfedcba9876543210); r.end();
    std::cout << "Core tests passed: expiry, exclusivity, recovery, rotation, rate limits, protocol bounds\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
