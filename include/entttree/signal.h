#pragma once

#include <functional>
#include <vector>

namespace entttree {

/**
 * @brief A simple typed signal that supports arbitrary callables.
 *
 * Unlike entt::sigh, this supports lambdas with captures.
 * The tradeoff is a std::function heap allocation per listener.
 */
template <typename... Args>
struct Signal {

    using Callback = std::function<void(Args...)>;
    using Id = size_t;

    Id connect(Callback cb) {
        Id id = _next_id++;
        _listeners.push_back({id, std::move(cb)});
        return id;
    }

    template <auto MemberFn, typename T>
    Id connect(T& instance) {
        return connect([&instance](Args... args) {
            (instance.*MemberFn)(args...);
        });
    }

    void disconnect(Id id) {
        std::erase_if(_listeners, [id](const Entry& e) { return e.id == id; });
    }

    void publish(Args... args) const {
        // copy in case a listener disconnects during publish
        auto snapshot = _listeners;
        for (auto& [id, cb] : snapshot) {
            cb(args...);
        }
    }

private:

    struct Entry {
        Id       id;
        Callback cb;
    };

    std::vector<Entry> _listeners;
    Id _next_id = 0;
};


} // namespace entttree
