// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/network.h"
#include <iphlpapi.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace karizmi {
Tick now() { return GetTickCount64(); }
namespace {
void nonblocking(SOCKET s) { u_long on = 1; if (ioctlsocket(s, FIONBIO, &on)) throw std::runtime_error("Cannot configure socket"); }
bool waitSocket(SOCKET s, bool write, Tick deadline) {
    fd_set fds; FD_ZERO(&fds); FD_SET(s, &fds);
    const Tick delay = deadline > now() ? deadline - now() : 0;
    timeval tv{long(delay / 1000), long(delay % 1000 * 1000)};
    const int result = select(0, write ? nullptr : &fds, write ? &fds : nullptr, nullptr, &tv);
    if (result < 0) throw SocketError("Network socket failed",WSAGetLastError()); return result > 0;
}
sockaddr_in endpoint(const std::string& ip, uint16_t port) {
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr) != 1) throw std::runtime_error("Invalid IPv4 address"); return addr;
}
std::string formatAddress(IN_ADDR addr) { char text[INET_ADDRSTRLEN]{}; if (!InetNtopA(AF_INET, &addr, text, sizeof(text))) throw std::runtime_error("Invalid address"); return text; }
}
Network::Network() { WSADATA data; if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("Windows networking unavailable"); }
Network::~Network() { WSACleanup(); }
Socket::~Socket() { close(); }
Socket::Socket(Socket&& other) noexcept : socket_(other.release()) {}
Socket& Socket::operator=(Socket&& other) noexcept { if (this != &other) { close(); socket_ = other.release(); } return *this; }
SOCKET Socket::release() { return std::exchange(socket_, INVALID_SOCKET); }
void Socket::close() { if (socket_ != INVALID_SOCKET) { closesocket(socket_); socket_ = INVALID_SOCKET; } }
void Socket::interrupt() { if (socket_ != INVALID_SOCKET) shutdown(socket_, SD_BOTH); }
Socket Socket::listen(uint16_t port) {
    if (!port) throw std::runtime_error("Invalid listening port");
    Socket s(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)); if (s.get() == INVALID_SOCKET) throw std::runtime_error("Cannot create listener");
    BOOL exclusive = TRUE; setsockopt(s.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    auto addr = endpoint("0.0.0.0", port);
    if (bind(s.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) || ::listen(s.get(), 4)) throw std::runtime_error("Listening port is unavailable");
    nonblocking(s.get()); return s;
}
Socket Socket::connect(const std::string& ip, uint16_t port, Tick deadline) {
    if (!parseAddress(ip)) throw std::runtime_error("Invalid peer address");
    Socket s(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)); if (s.get() == INVALID_SOCKET) throw std::runtime_error("Cannot create connection");
    nonblocking(s.get()); auto addr = endpoint(ip, port);
    int result = ::connect(s.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR) {const int error=WSAGetLastError();if(error!=WSAEWOULDBLOCK)throw SocketError("Cannot connect to peer",error);}
    if (!waitSocket(s.get(), true, deadline)) throw SocketError("Connection timed out",WSAETIMEDOUT);
    int error = 0, size = sizeof(error); getsockopt(s.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size);
    if (error) throw SocketError("Connection refused or blocked",error); return s;
}
Socket Socket::accept(std::string& peer, Tick deadline) {
    if (!readable(deadline)) return Socket(); sockaddr_in addr{}; int length = sizeof(addr);
    Socket s(::accept(socket_, reinterpret_cast<sockaddr*>(&addr), &length));
    if (s.get() == INVALID_SOCKET) return s; nonblocking(s.get()); peer = formatAddress(addr.sin_addr); return s;
}
bool Socket::readable(Tick deadline) const { return waitSocket(socket_, false, deadline); }
void Socket::write(std::span<const uint8_t> bytes, Tick deadline) {
    while (!bytes.empty()) {
        if (!waitSocket(socket_, true, deadline)) throw std::runtime_error("Network write timed out");
        int sent = ::send(socket_, reinterpret_cast<const char*>(bytes.data()), int(std::min<size_t>(bytes.size(), 65536)), 0);
        if (sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (sent <= 0) throw std::runtime_error("Connection lost"); bytes = bytes.subspan(sent);
    }
}
size_t Socket::readSome(std::span<uint8_t> bytes, Tick deadline) {
    for (;;) {
        if (!readable(deadline)) throw SocketError("Network read timed out",WSAETIMEDOUT);
        int got = recv(socket_, reinterpret_cast<char*>(bytes.data()), int(std::min<size_t>(bytes.size(), 65536)), 0);
        if (got == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (got <= 0) throw SocketError("Connection lost",got==0?WSAECONNRESET:WSAGetLastError()); return size_t(got);
    }
}
void Socket::read(std::span<uint8_t> bytes, Tick deadline) { while (!bytes.empty()) bytes = bytes.subspan(readSome(bytes, deadline)); }
void sendPacket(Socket& socket, const Packet& packet, Tick deadline) { socket.write(encode(packet), deadline); }
Packet readPacket(Socket& socket, Tick deadline, size_t maximum) {
    std::vector<uint8_t> bytes(HeaderSize); socket.read(bytes, deadline);
    auto length = payloadLength(bytes, maximum); bytes.resize(HeaderSize + length);
    socket.read(std::span(bytes).subspan(HeaderSize), deadline); return decode(bytes, maximum);
}
std::vector<Adapter> adapters() {
    ULONG size = 16384; std::vector<uint8_t> buffer(size);
    auto* first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, first, &size);
    if (result == ERROR_BUFFER_OVERFLOW) { buffer.resize(size); first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, first, &size); }
    if (result != NO_ERROR) return {};
    std::vector<Adapter> out;
    for (auto* a = first; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211)) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET || u->OnLinkPrefixLength > 30) continue;
            auto ip = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;
            if (!parseAddress(formatAddress(ip))) continue;
            uint32_t mask = u->OnLinkPrefixLength ? 0xffffffffu << (32 - u->OnLinkPrefixLength) : 0;
            IN_ADDR network{}, broadcast{}; network.s_addr = htonl(ntohl(ip.s_addr) & mask); broadcast.s_addr = htonl(ntohl(ip.s_addr) | ~mask);
            out.push_back({formatAddress(ip), formatAddress(broadcast), formatAddress(network) + "/" + std::to_string(u->OnLinkPrefixLength), a->IfIndex});
        }
    }
    return out;
}
bool validDeviceLabel(const std::string& label) {
    return !label.empty() && label.size() <= 128 &&
        std::none_of(label.begin(),label.end(),[](unsigned char c) { return c < 32 || c == 127; }) &&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,label.data(),int(label.size()),nullptr,0) > 0;
}
std::vector<Candidate> discover(Tick deadline, const std::atomic_bool& cancelled) {
    auto interfaces = adapters(); if (interfaces.empty()) throw std::runtime_error("No supported IPv4 LAN/WLAN adapter. IPv6-only networks are not supported.");
    std::vector<Socket> sockets; InstanceId nonce{}; randomBytes(nonce);
    auto query = encode({Message::DiscoveryQuery, 0, {nonce.begin(), nonce.end()}});
    for (const auto& a : interfaces) {
        Socket s(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)); BOOL yes = TRUE;
        setsockopt(s.get(), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
        auto local = endpoint(a.address, 0); if (bind(s.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local))) continue;
        nonblocking(s.get()); auto destination = endpoint(a.broadcast, DiscoveryPort);
        sendto(s.get(), reinterpret_cast<const char*>(query.data()), int(query.size()), 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
        sockets.push_back(std::move(s));
    }
    std::vector<Candidate> found;
    while (now() < deadline && !cancelled) {
        for (auto& socket : sockets) {
            if (!socket.readable(now())) continue;
            std::array<uint8_t, 512> buffer; sockaddr_in source{}; int size = sizeof(source);
            int got = recvfrom(socket.get(), reinterpret_cast<char*>(buffer.data()), int(buffer.size()), 0, reinterpret_cast<sockaddr*>(&source), &size);
            if (got <= 0) continue;
            try {
                auto p = decode(std::span(buffer).first(got), 256);
                if (p.type != Message::DiscoveryReply || p.sequence != 0) continue;
                Reader r(p.payload); auto echo = r.bytes(16); if (!equalSecret(echo, nonce)) continue;
                auto rawId = r.bytes(16); Candidate c{}; std::copy(rawId.begin(), rawId.end(), c.instance.begin());
                c.port = r.u16(); c.label = r.text(); r.end(); c.address = formatAddress(source.sin_addr); if (!c.port || !parseAddress(c.address) || !validDeviceLabel(c.label)) continue;
                if (found.size() >= 256) continue;
                auto existing = std::find_if(found.begin(),found.end(),[&](const auto& x) { return x.instance == c.instance; });
                if (existing == found.end()) found.push_back(c); else *existing = c;
            } catch (...) { /* Ignore malformed, unauthenticated discovery hints. */ }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return found;
}
DiscoveryResponder::DiscoveryResponder(InstanceId instance, uint16_t port, std::string label) {
    if (!validDeviceLabel(label)) throw std::runtime_error("Invalid discovery device name");
    // Bind synchronously so the UI receives errors instead of silent thread failure.
    Socket s(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)); auto local = endpoint("0.0.0.0", DiscoveryPort);
    BOOL exclusive = TRUE; setsockopt(s.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    if (bind(s.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local))) throw std::runtime_error("Discovery port is unavailable"); nonblocking(s.get());
    thread_ = std::thread([this, instance, port, label, socket = std::move(s)]() mutable {
        Tick window = now(); unsigned replies = 0;
        while (!stop_) try {
            if (!socket.readable(now() + 100)) continue;
            std::array<uint8_t, 512> bytes; sockaddr_in source{}; int size = sizeof(source);
            int got = recvfrom(socket.get(), reinterpret_cast<char*>(bytes.data()), int(bytes.size()), 0, reinterpret_cast<sockaddr*>(&source), &size);
            if (got <= 0 || !enabled_) continue;
            if (now() - window >= 1000) { window = now(); replies = 0; }
            if (replies >= 20) continue;
            auto p = decode(std::span(bytes).first(got), 128);
            if (p.type != Message::DiscoveryQuery || p.sequence != 0 || p.payload.size() != 16) continue;
            Writer w; w.bytes(p.payload); w.bytes(instance); w.u16(port); w.text(label); auto response = encode({Message::DiscoveryReply, 0, w.data});
            sendto(socket.get(), reinterpret_cast<const char*>(response.data()), int(response.size()), 0, reinterpret_cast<sockaddr*>(&source), size); ++replies;
        } catch (...) { if (stop_) break; }
    });
}
DiscoveryResponder::~DiscoveryResponder() { stop_ = true; if (thread_.joinable()) thread_.join(); }

SecureConnection::SecureConnection(Socket socket, bool server, const Secret& key, const std::string& identity, Tick deadline) : socket_(std::move(socket)) {
    tls_ = std::make_unique<TlsChannel>(server, key, identity,
        [this](auto bytes) { socket_.write(bytes, now() + 5000); }, [this](auto bytes) { parse(bytes); });
    std::array<uint8_t, 16384> buffer;
    while (!tls_->active()) { auto got = socket_.readSome(buffer, deadline); tls_->receive(std::span(buffer).first(got)); }
    lastReceived_ = now(); thread_ = std::thread([this] { pump(); });
}
SecureConnection::~SecureConnection() { stop(); if (thread_.joinable()) thread_.join(); }
void SecureConnection::stop() { alive_ = false; socket_.interrupt(); changed_.notify_all(); }
Secret SecureConnection::exporter(const std::string& purpose) { std::lock_guard lock(tlsMutex_); return tls_->exporter(purpose); }
void SecureConnection::parse(std::span<const uint8_t> bytes) {
    if (partial_.size() + bytes.size() > MaxPayload * 2) throw std::runtime_error("Encrypted frame buffer exceeded");
    partial_.insert(partial_.end(), bytes.begin(), bytes.end());
    while (partial_.size() >= HeaderSize) {
        const auto length = payloadLength(std::span(partial_).first(HeaderSize));
        if (partial_.size() < HeaderSize + length) break;
        auto packet = decode(std::span(partial_).first(HeaderSize + length));
        if (!receiveSequence_.accept(packet.sequence)) throw std::runtime_error("Replayed or reordered message");
        partial_.erase(partial_.begin(), partial_.begin() + HeaderSize + length);
        lastReceived_ = now();
        std::lock_guard lock(mutex_);
        if (packet.type == Message::Heartbeat) { if (!packet.payload.empty()) throw std::runtime_error("Invalid heartbeat"); }
        else if (packet.type == Message::Rfb) {
            if (rfb_.size() + packet.payload.size() > 16 * 1024 * 1024) throw std::runtime_error("Screen queue exceeded");
            rfb_.insert(rfb_.end(), packet.payload.begin(), packet.payload.end());
        } else {
            if(packet.type==Message::Disconnect && packet.payload.empty())peerDisconnected_=true;
            if (incoming_.size() >= 64) throw std::runtime_error("Control queue exceeded");
            incoming_.push_back(std::move(packet));
        }
        changed_.notify_all();
    }
}
void SecureConnection::pump() {
    Tick heartbeat = now(); std::array<uint8_t, 16384> buffer;
    try {
        while (alive_) {
            if (now() - lastReceived_ >= HeartbeatTimeout) throw std::runtime_error("Peer heartbeat lost");
            if (now() - heartbeat >= HeartbeatInterval) { send(Message::Heartbeat); heartbeat = now(); }
            for (unsigned i = 0; i < 8; ++i) {
                std::optional<Packet> packet;
                { std::lock_guard lock(mutex_); if (!outgoing_.empty()) { packet = std::move(outgoing_.front()); outgoing_.pop_front(); queuedBytes_ -= packet->payload.size(); } }
                if (!packet) break;
                { std::lock_guard lock(tlsMutex_); tls_->send(encode(*packet)); }
                { std::lock_guard lock(mutex_);transmitted_=packet->sequence+1;changed_.notify_all(); }
            }
            if (socket_.readable(now() + 10)) {
                auto got = socket_.readSome(buffer, now() + 1000); std::lock_guard lock(tlsMutex_); tls_->receive(std::span(buffer).first(got));
            }
        }
    } catch (...) { /* Owner sees alive()==false and starts its bounded recovery. */ }
    alive_ = false; changed_.notify_all();
}
void SecureConnection::send(Message type, std::span<const uint8_t> bytes) {
    std::lock_guard lock(mutex_);
    if (!alive_ || bytes.size() > MaxPayload || queuedBytes_ + bytes.size() > 16 * 1024 * 1024 || outgoing_.size() >= 1024) throw std::runtime_error("Connection unavailable or send queue full");
    outgoing_.push_back({type, sendSequence_.next(), {bytes.begin(), bytes.end()}}); queuedBytes_ += bytes.size();++enqueued_;
}
bool SecureConnection::flush(Tick deadline) {
    std::unique_lock lock(mutex_);const auto target=enqueued_;
    changed_.wait_for(lock,std::chrono::milliseconds(deadline>now()?deadline-now():0),[&]{return transmitted_>=target || !alive_;});
    return transmitted_>=target;
}
std::optional<Packet> SecureConnection::receive(Tick deadline) {
    std::unique_lock lock(mutex_);
    changed_.wait_for(lock, std::chrono::milliseconds(deadline > now() ? deadline - now() : 0), [&] { return !incoming_.empty() || !alive_; });
    if (incoming_.empty()) return {}; auto result = std::move(incoming_.front()); incoming_.pop_front(); return result;
}
int SecureConnection::readRfbBlocking(void* data,int length) {
    while(alive_) { int count=readRfb(data,length,now()+1000); if(count>=0) return count; }
    return -1;
}
int SecureConnection::readRfb(void* data, int length, Tick deadline) {
    if (length <= 0) return 0; std::unique_lock lock(mutex_);
    changed_.wait_for(lock, std::chrono::milliseconds(deadline > now() ? deadline - now() : 0), [&] { return !rfb_.empty() || !alive_; });
    if (!alive_ || rfb_.empty()) return -1;
    auto size = std::min<size_t>(length, rfb_.size()); auto* out = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { out[i] = rfb_.front(); rfb_.pop_front(); } return int(size);
}
bool SecureConnection::writeRfb(const void* data, size_t length) {
    auto bytes = std::span(static_cast<const uint8_t*>(data), length);
    try { while (!bytes.empty()) { auto part = bytes.first(std::min(bytes.size(), MaxPayload)); send(Message::Rfb, part); bytes = bytes.subspan(part.size()); } return true; } catch (...) { return false; }
}
bool SecureConnection::rfbReadable(Tick deadline) {
    std::unique_lock lock(mutex_);
    changed_.wait_for(lock, std::chrono::milliseconds(deadline > now() ? deadline - now() : 0), [&] { return !rfb_.empty() || !alive_; }); return alive_ && !rfb_.empty();
}
}
