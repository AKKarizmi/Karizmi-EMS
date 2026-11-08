// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/network.h"
#include "karizmi/firewall.h"
#include "karizmi/classroom.h"
#include "karizmi/pairing.h"
#include "karizmi/approvals.h"
#include "karizmi/runtime.h"
#include <future>
#include <iostream>
#include <stdexcept>
using namespace karizmi;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Failed: " #x); } while (0)

namespace karizmi {
struct RuntimeRecoveryTest {
    struct IdleEngine : Engine {
        bool inputAvailable() const override {return true;}
        void attach(std::shared_ptr<SecureConnection>,const std::string&) override {}
        void detach() override {}
        void control(bool) override {}
        void releaseInput() override {}
    };
    static void unavailableAccessRetainsReservation() {
        IdleEngine engine;Runtime runtime(Role::Student,engine);
        InstanceId peer{};Secret token{};randomBytes(peer);randomBytes(token);
        runtime.peer_=peer;
        runtime.session_.restoreReservation(peer,"Reserved student",token,true,now());
        // An unavailable firewall broker exercises the same failed-access branch
        // as ERROR_NETWORK_UNREACHABLE with no eligible adapters. No UAC/rules.
        auto recovery=std::async(std::launch::async,[&]{return runtime.recoverStudent();});
        const auto deadline=now()+3000;bool waiting=false;
        while(now()<deadline) {
            auto state=runtime.display();
            waiting=state.status.starts_with("R105");
            if(waiting)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto reserved=runtime.session_.authenticates(peer,token,now());
        const auto noFreshCode=runtime.display().code.empty();
        const auto stillRunning=recovery.wait_for(std::chrono::milliseconds(1200))==std::future_status::timeout;
        runtime.cancel_=true;auto stream=recovery.get();
        CHECK(waiting && reserved && noFreshCode && stillRunning && !stream);
        CHECK(runtime.display().session.state==State::Recovering);
        runtime.disconnect();CHECK(runtime.display().session.state==State::Ended);
        CHECK(!runtime.session_.authenticates(peer,token,now()));erase(token);
    }
    static void engineFailureEntersRecovery(std::shared_ptr<SecureConnection> stream) {
        struct FailingEngine : IdleEngine {
            void attach(std::shared_ptr<SecureConnection>,const std::string&) override {
                throw std::runtime_error("Simulated engine failure before pump detects outage");
            }
        } engine;
        Runtime runtime(Role::Teacher,engine);InstanceId peer{};Secret token{};randomBytes(peer);randomBytes(token);
        runtime.peer_=peer;runtime.token_=token;
        runtime.session_.restoreReservation(peer,"Reserved teacher",token,true,now());
        runtime.reconnectNow();CHECK(runtime.display().status.starts_with("R011"));
        CHECK(runtime.session_.prepareRotation(peer,token,token,now()));
        CHECK(runtime.session_.finishRotation(token,now()));
        auto active=std::async(std::launch::async,[&]{runtime.activeLoop(stream);});
        const auto deadline=now()+3000;bool recovering=false,recorded=false;
        while(now()<deadline) {
            auto state=runtime.display();
            recovering=state.session.state==State::Recovering;
            for(const auto& stage:state.recoveryTrace)if(stage.find(" R120 ")!=std::string::npos)recorded=true;
            if(recovering && recorded)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const bool reserved=runtime.session_.authenticates(peer,token,now());
        runtime.disconnect();active.get();
        CHECK(recovering && recorded && reserved && !stream->alive());
        CHECK(runtime.display().session.state==State::Ended);erase(token);
    }
};
}

// Test-only loopback sockets: production discovery/connection rejects loopback.
static std::pair<Socket, Socket> socketPair() {
    Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    CHECK(::listen(listener.get(), 1) == 0);
    int length = sizeof(address); CHECK(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) == 0);
    Socket client(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    CHECK(::connect(client.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    Socket server(::accept(listener.get(), nullptr, nullptr)); CHECK(server.get() != INVALID_SOCKET);
    u_long enabled = 1;
    CHECK(ioctlsocket(client.get(), FIONBIO, &enabled) == 0); CHECK(ioctlsocket(server.get(), FIONBIO, &enabled) == 0);
    return {std::move(client), std::move(server)};
}
int main() try {
    Network network; Secret key{}; randomBytes(key);
    RuntimeRecoveryTest::unavailableAccessRetainsReservation();
    {
        auto pair=socketPair();
        auto task=std::async(std::launch::async,[&]{return std::make_unique<SecureConnection>(std::move(pair.second),true,key,"runtime-failure-test",now()+5000);});
        auto channel=std::make_shared<SecureConnection>(std::move(pair.first),false,key,"runtime-failure-test",now()+5000);
        auto peer=task.get();RuntimeRecoveryTest::engineFailureEntersRecovery(channel);
    }
    {
        const auto testNamespace=L"RoundTrip-"+std::to_wstring(GetCurrentProcessId());
        Approval record;record.role=Role::Teacher;randomBytes(record.local);randomBytes(record.peer);
        record.label="Approved PC";record.address="192.168.1.15";randomBytes(record.current);randomBytes(record.pending);record.hasPending=true;
        { ApprovalStore store(Role::Teacher,testNamespace);CHECK(store.save(record)); }
        ApprovalStore restored(Role::Teacher,testNamespace);auto records=restored.load();CHECK(records.size()==1);
        CHECK(records[0].local==record.local && records[0].peer==record.peer && equalSecret(records[0].current,record.current) && equalSecret(records[0].pending,record.pending) && records[0].hasPending);
        ApprovalStore otherRole(Role::Student,testNamespace);CHECK(otherRole.load().empty());
        record.label="bad\nlabel";CHECK(!restored.save(record));
        restored.remove(record.peer);CHECK(restored.load().empty());restored.clear();otherRole.clear();
        record.label="Bounded PC";
        for(unsigned index=0;index<11;++index) { record.peer[0]=uint8_t(index);CHECK(restored.save(record)==(index<10)); }
        CHECK(restored.load().size()==10);restored.clear();
    }
    auto sockets = socketPair();
    auto serverTask = std::async(std::launch::async, [&] {
        return std::make_unique<SecureConnection>(std::move(sockets.second), true, key, "network-test", now() + 5000);
    });
    SecureConnection client(std::move(sockets.first), false, key, "network-test", now() + 5000);
    auto server = serverTask.get();
    CHECK(equalSecret(client.exporter("test"), server->exporter("test")));
    std::vector<uint8_t> frame(150000);
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = uint8_t(i * 37);
    CHECK(server->writeRfb(frame.data(), frame.size()));
    std::vector<uint8_t> received(frame.size()); size_t offset = 0;
    while (offset < received.size()) {
        auto count = client.readRfb(received.data() + offset, int(received.size() - offset), now() + 5000);
        CHECK(count > 0); offset += size_t(count);
    }
    CHECK(frame == received);
    // A quiet desktop can have no new pixels while authenticated heartbeats continue.
    auto idleReader=std::async(std::launch::async,[&]{uint8_t pixel{}; CHECK(client.readRfbBlocking(&pixel,1)==1); return pixel;});
    CHECK(idleReader.wait_for(std::chrono::milliseconds(1200))==std::future_status::timeout);
    uint8_t wakePixel=42; CHECK(server->writeRfb(&wakePixel,1)); CHECK(idleReader.get()==42);
    std::atomic_int decision=0; std::atomic_bool cancelConfirmation=false;
    auto confirmation=std::async(std::launch::async,[&]{return exchangeConfirmation(client,decision,cancelConfirmation,now()+3000);});
    server->send(Message::Confirm);
    std::this_thread::sleep_for(std::chrono::milliseconds(150)); decision=1;
    auto confirmed=server->receive(now()+3000); CHECK(confirmed && confirmed->type==Message::Confirm);
    server->send(Message::Rotate,key); // Must remain queued for the next protocol stage.
    CHECK(confirmation.get()); auto rotated=client.receive(now()+3000); CHECK(rotated && rotated->type==Message::Rotate);
    const std::array<uint8_t, 1> permission{1}; client.send(Message::ControlRequest, permission);
    auto request = server->receive(now() + 5000);
    CHECK(request && request->type == Message::ControlRequest && request->payload == std::vector<uint8_t>{1});
    // Transport loss must wake blocked engine readers without waiting for their deadline.
    auto pending = std::async(std::launch::async, [&] { uint8_t byte; return client.readRfb(&byte, 1, now() + 30000); });
    server->stop(); CHECK(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready); CHECK(pending.get() == -1);
    CHECK(!client.alive());
    // A wrong reconnect/transport secret cannot establish a session.
    auto rejected = socketPair(); auto wrong = key; wrong[0] ^= 1;
    auto rejectedServer = std::async(std::launch::async, [&] {
        try { SecureConnection channel(std::move(rejected.second), true, key, "network-test", now() + 2000); return false; }
        catch (...) { return true; }
    });
    bool failed = false;
    try { SecureConnection channel(std::move(rejected.first), false, wrong, "network-test", now() + 2000); }
    catch (...) { failed = true; }
    CHECK(failed); CHECK(rejectedServer.get());
    CHECK(validDeviceLabel("PC-001")); CHECK(!validDeviceLabel(""));
    CHECK(!validDeviceLabel(std::string(129,'x'))); CHECK(!validDeviceLabel("spoof\nname")); CHECK(!validDeviceLabel("\xff"));
    DiscoveryCatalog catalog;
    Candidate hint{{},"192.168.1.2",BootstrapPort,"PC-001"}; catalog.observe({hint},100);
    hint.address="192.168.1.3"; catalog.observe({hint},200);
    CHECK(catalog.current(200).size()==1); CHECK(catalog.current(200)[0].address==hint.address);
    CHECK(catalog.current(15199).size()==1); CHECK(catalog.current(15200).empty());
    std::vector<Candidate> flood;
    for(unsigned i=0;i<300;++i) { auto entry=hint; entry.instance[0]=uint8_t(i); entry.instance[1]=uint8_t(i>>8); flood.push_back(entry); }
    catalog.observe(flood,16000); CHECK(catalog.current(16000).size()==256);
    auto invalid=hint; invalid.label="bad\nlabel"; DiscoveryCatalog malformed; malformed.observe({invalid},0); CHECK(malformed.current(0).empty());
    {
        ClassroomResources resources; std::atomic_bool cancelled=false;
        for(int i=0;i<4;++i) CHECK(resources.acquire(cancelled));
        auto fifth=std::async(std::launch::async,[&]{return resources.acquire(cancelled);});
        CHECK(fifth.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout);
        cancelled=true; CHECK(!fifth.get()); for(int i=0;i<4;++i) resources.release();
    }
    // Ten independent encrypted transports coexist; disconnect/control one cannot route to another.
    std::vector<std::unique_ptr<SecureConnection>> teachers,students;
    for(unsigned i=0;i<10;++i) {
        auto pair=socketPair(); Secret peerKey{}; randomBytes(peerKey);
        auto task=std::async(std::launch::async,[&]{return std::make_unique<SecureConnection>(std::move(pair.second),true,peerKey,"classroom-v2-test",now()+5000);});
        teachers.push_back(std::make_unique<SecureConnection>(std::move(pair.first),false,peerKey,"classroom-v2-test",now()+5000));
        students.push_back(task.get()); erase(peerKey);
    }
    for(unsigned i=0;i<10;++i) { uint8_t pixel=uint8_t(i); CHECK(students[i]->writeRfb(&pixel,1)); }
    for(unsigned i=0;i<10;++i) { uint8_t pixel=255; CHECK(teachers[i]->readRfb(&pixel,1,now()+5000)==1); CHECK(pixel==i); }
    teachers[3]->send(Message::ControlRequest,permission);
    CHECK(students[3]->receive(now()+5000)->type==Message::ControlRequest);
    for(unsigned i=0;i<10;++i) if(i!=3) CHECK(!students[i]->receive(now()+10));
    students[3]->stop();
    for(unsigned i=0;i<10;++i) if(i!=3) { CHECK(teachers[i]->alive()); uint8_t pixel=uint8_t(i+10); CHECK(students[i]->writeRfb(&pixel,1)); CHECK(teachers[i]->readRfb(&pixel,1,now()+5000)==1); CHECK(pixel==i+10); }
    teachers[9]->send(Message::Disconnect);CHECK(teachers[9]->flush(now()+3000));
    auto goodbye=students[9]->receive(now()+3000);CHECK(goodbye && goodbye->type==Message::Disconnect && students[9]->peerDisconnected());
    FirewallRequest rule; CHECK(validateFirewallRequest(rule));
    rule.mode = AccessMode::Session; CHECK(!validateFirewallRequest(rule));
    strcpy_s(rule.peer, "192.168.1.20"); CHECK(validateFirewallRequest(rule));
    strcpy_s(rule.peer, "192.168.1.0/24"); CHECK(!validateFirewallRequest(rule));
    rule.mode = AccessMode::Bootstrap; rule.stream = rule.bootstrap; CHECK(!validateFirewallRequest(rule));
    std::cout << "Encrypted byte stream, control framing, disconnect, wrong key and firewall request checks passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
