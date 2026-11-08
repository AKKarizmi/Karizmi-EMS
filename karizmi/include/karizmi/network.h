// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "security.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <stdexcept>

namespace karizmi {
Tick now();
class SocketError : public std::runtime_error {
public:
    SocketError(const char* message,int code) : std::runtime_error(message),code(code) {}
    const int code;
};
class Network {
public: Network(); ~Network();
};
class Socket {
public:
    explicit Socket(SOCKET socket = INVALID_SOCKET) : socket_(socket) {}
    ~Socket(); Socket(const Socket&) = delete; Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept; Socket& operator=(Socket&& other) noexcept;
    SOCKET get() const { return socket_; }
    SOCKET release(); void close(); void interrupt();
    static Socket listen(uint16_t port);
    static Socket connect(const std::string& ip, uint16_t port, Tick deadline);
    Socket accept(std::string& peer, Tick deadline);
    bool readable(Tick deadline) const;
    void write(std::span<const uint8_t> bytes, Tick deadline);
    void read(std::span<uint8_t> bytes, Tick deadline);
    size_t readSome(std::span<uint8_t> bytes, Tick deadline);
private: SOCKET socket_;
};
void sendPacket(Socket& socket, const Packet& packet, Tick deadline);
Packet readPacket(Socket& socket, Tick deadline, size_t maximum = MaxBootstrapPayload);
struct Adapter { std::string address, broadcast, subnet; unsigned index; };
std::vector<Adapter> adapters();
struct Candidate { InstanceId instance; std::string address; uint16_t port; std::string label; };
bool validDeviceLabel(const std::string& label);
std::vector<Candidate> discover(Tick deadline, const std::atomic_bool& cancelled);
class DiscoveryResponder {
public:
    DiscoveryResponder(InstanceId instance, uint16_t bootstrapPort, std::string label = "Student");
    ~DiscoveryResponder();
    void enabled(bool value) { enabled_ = value; }
private:
    std::atomic_bool stop_ = false, enabled_ = true;
    std::thread thread_;
};

// Owns the actual encrypted socket. RFB is a framed channel in this connection,
// never a localhost TCP bridge. Only the reactor thread accesses Botan.
class SecureConnection {
public:
    SecureConnection(Socket socket, bool server, const Secret& key, const std::string& identity, Tick deadline);
    ~SecureConnection();
    void send(Message type, std::span<const uint8_t> payload = {});
    bool flush(Tick deadline);
    bool peerDisconnected() const { return peerDisconnected_; }
    std::optional<Packet> receive(Tick deadline);
    int readRfb(void* data, int length, Tick deadline);
    int readRfbBlocking(void* data, int length);
    bool writeRfb(const void* data, size_t length);
    bool rfbReadable(Tick deadline);
    void stop();
    bool alive() const { return alive_; }
    Secret exporter(const std::string& purpose);
    SOCKET nativeSocket() const { return socket_.get(); }
private:
    void pump(); void parse(std::span<const uint8_t> bytes);
    Socket socket_;
    std::unique_ptr<TlsChannel> tls_;
    std::mutex mutex_, tlsMutex_;
    std::condition_variable changed_;
    std::deque<Packet> outgoing_, incoming_;
    std::deque<uint8_t> rfb_;
    std::vector<uint8_t> partial_;
    size_t queuedBytes_ = 0;
    Sequence sendSequence_, receiveSequence_;
    std::atomic_bool alive_ = true;
    std::atomic_bool peerDisconnected_ = false;
    uint64_t enqueued_=0,transmitted_=0;
    std::thread thread_;
    Tick lastReceived_ = 0;
};
}
