// SPDX-License-Identifier: GPL-3.0-or-later
#include "karizmi/classroom.h"
namespace karizmi {
void DiscoveryCatalog::observe(const std::vector<Candidate>& found,Tick time) {
    for(auto it=entries_.begin();it!=entries_.end();) {
        if(time-it->second.seen>=15000) it=entries_.erase(it); else ++it;
    }
    for(const auto& candidate:found) if(validDeviceLabel(candidate.label) && parseAddress(candidate.address) && candidate.port &&
        (entries_.count(candidate.instance) || entries_.size()<256)) entries_[candidate.instance]={candidate,time};
}
std::vector<Candidate> DiscoveryCatalog::current(Tick time) const {
    std::vector<Candidate> result;
    for(const auto& [id,entry]:entries_) if(time-entry.seen<15000) result.push_back(entry.candidate);
    return result;
}
ClassroomResources::ClassroomResources() : scanner_([this] {
    Tick next = 0;
    while (!stop_) {
        if (scan_.exchange(false) || now() >= next) {
            next = now() + 5000;
            try {
                auto found = discover(now() + 1000,stop_);
                std::lock_guard lock(mutex_);
                catalog_.observe(found,now());
                diagnostic_ = "Only Karizmi Students are listed. Names are unverified until pairing. Update both apps if a device is missing; older protocol versions are incompatible.";
            } catch (const std::exception& error) { std::lock_guard lock(mutex_); diagnostic_ = error.what(); }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}) {}
ClassroomResources::~ClassroomResources() { stop_ = true; if (scanner_.joinable()) scanner_.join(); }
void ClassroomResources::scanNow() { scan_ = true; }
std::vector<Candidate> ClassroomResources::candidates() const {
    std::lock_guard lock(mutex_); return catalog_.current(now());
}
std::string ClassroomResources::diagnostic() const { std::lock_guard lock(mutex_); return diagnostic_; }
bool ClassroomResources::acquire(const std::atomic_bool& cancelled) {
    while (!stop_ && !cancelled) {
        { std::lock_guard lock(mutex_); if (attempts_ < 4) { ++attempts_; return true; } }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
void ClassroomResources::release() { std::lock_guard lock(mutex_); if (attempts_) --attempts_; }
FirewallResult ClassroomResources::setup(HWND owner, bool remove) {
    FirewallRequest request; request.mode = remove ? AccessMode::Remove : AccessMode::Bootstrap;
    auto result = firewall_.configure(owner,request);
    if (!result.error) ready_ = !remove;
    return result;
}
}
