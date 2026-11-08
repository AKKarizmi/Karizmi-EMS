// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "firewall.h"
#include <functional>
namespace karizmi {
class DiscoveryCatalog {
public:
    void observe(const std::vector<Candidate>& found,Tick time);
    std::vector<Candidate> current(Tick time) const;
private:
    struct Entry { Candidate candidate; Tick seen; };
    std::map<InstanceId,Entry> entries_;
};
// One owner per Teacher application; never one discovery scanner/helper per peer.
class ClassroomResources {
public:
    ClassroomResources();
    ~ClassroomResources();
    std::vector<Candidate> candidates() const;
    void scanNow();
    std::string diagnostic() const;
    bool acquire(const std::atomic_bool& cancelled);
    void release();
    bool ready() const { return ready_; }
    FirewallResult setup(HWND owner, bool remove);
private:
    mutable std::mutex mutex_;
    DiscoveryCatalog catalog_;
    std::string diagnostic_;
    std::atomic_bool stop_ = false, scan_ = true, ready_ = false;
    unsigned attempts_ = 0;
    Network network_;
    FirewallBroker firewall_{Role::Teacher};
    std::thread scanner_;
};
class ClassroomAttempt {
public:
    ClassroomAttempt(std::shared_ptr<ClassroomResources> resources, const std::atomic_bool& cancel) : resources_(std::move(resources)) {
        held_ = !resources_ || resources_->acquire(cancel);
    }
    ~ClassroomAttempt() { if (held_ && resources_) resources_->release(); }
    explicit operator bool() const { return held_; }
private:
    std::shared_ptr<ClassroomResources> resources_; bool held_;
};
}
