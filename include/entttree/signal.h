/**
 * @file signal.h
 * @brief A simple typed signal/event system.
 */

#pragma once

#include <functional>
#include <vector>

namespace entttree {

/**
 * @brief A simple typed signal that supports arbitrary callables.
 *
 * Unlike `entt::sigh`, this supports lambdas with captures.
 * The tradeoff is a `std::function` heap allocation per listener.
 *
 * @tparam Args The argument types published with each signal emission.
 */
template <typename... Args>
struct Signal {

    /// Type-erased callback stored per listener.
    using Callback = std::function<void(Args...)>;
    /// Opaque handle returned by connect(), used to disconnect later.
    using Id = size_t;

    /**
     * @brief Register a callable to be invoked when the signal is published.
     * @param cb The callback to register.
     * @return An Id that can be passed to disconnect().
     */
    Id connect(Callback cb) {
        Id id = _next_id++;
        _listeners.push_back({id, std::move(cb)});
        return id;
    }

    /**
     * @brief Register a member function as a listener.
     *
     * @tparam MemberFn A pointer-to-member-function, e.g. `&MyClass::on_event`.
     * @tparam T        The class type that owns the member function.
     * @param instance  The object on which the member function will be called.
     *                  Must outlive the connection.
     * @return An Id that can be passed to disconnect().
     */
    template <auto MemberFn, typename T>
    Id connect(T& instance) {
        return connect([&instance](Args... args) {
            (instance.*MemberFn)(args...);
        });
    }

    /**
     * @brief Remove a previously registered listener.
     * @param id The connection Id returned by connect().
     */
    void disconnect(Id id) {
        std::erase_if(_listeners, [id](const Entry& e) { return e.id == id; });
    }

    /**
     * @brief Invoke all registered listeners with the given arguments.
     *
     * A snapshot of the listener list is taken before iteration, so it is
     * safe for a listener to disconnect itself during the call.
     */
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
