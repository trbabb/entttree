/**
 * @file generator.h
 * @brief Coroutine-based generator and related wrapper types.
 */

#pragma once

#include <coroutine>
#include <optional>
#include <variant>

namespace entttree {

/**
 * @brief A C++20 coroutine-based generator that lazily yields values of type `T`.
 *
 * A coroutine returning `Generator<T>` can use `co_yield` to produce values and
 * `co_return` to finish. The consumer drives iteration with `operator++` and
 * `operator bool`:
 *
 * @code
 * Generator<int> range(int lo, int hi) {
 *     for (int i = lo; i < hi; ++i) co_yield i;
 * }
 * for (auto g = range(0, 5); g; ++g) {
 *     std::cout << *g << "\n";
 * }
 * @endcode
 *
 * @tparam T The type of value yielded by the generator.
 * @tparam V Optional return-value type (defaults to void / `std::monostate`).
 */
template <typename T, typename V=void>
struct Generator {

    using ret_t = std::conditional_t<std::is_void_v<V>, std::monostate, V>;

    struct promise_type {
    private:
        std::optional<T>                    _value  = std::nullopt;
        std::optional<ret_t>                _ret    = std::nullopt;
        std::coroutine_handle<promise_type> _handle;

        friend struct Generator<T>;

    public:

        Generator<T> get_return_object() {
            return Generator<T>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never  initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() {
            _ret.emplace(std::monostate{});
        }

        template <typename U>
        std::suspend_always yield_value(U&& value) {
            _value.emplace(std::forward<U>(value));
            return {};
        }

        void unhandled_exception() {}

        bool has_value() const { return _value.has_value(); }

        const T& value() const { return *_value; }
              T& value()       { return *_value; }

        ret_t& get_return_value()             { return *_ret; }
        const ret_t& get_return_value() const { return *_ret; }
        ret_t move_return_value()             { return std::move(*_ret); }

        operator bool() const { return has_value(); }

    }; // promise_type

    Generator(const Generator&) = delete;
    Generator(Generator&& other):
        _handle(other._handle)
    {
        other._handle = {};
    }

    ~Generator() {
        if (_handle) _handle.destroy();
    }

    Generator& operator=(const Generator&) = delete;
    Generator& operator=(Generator&& other) {
        if (this == &other) return *this;
        if (_handle) _handle.destroy();
        _handle = other._handle;
        other._handle = {};
        return *this;
    }

private:

    bool _maybe_finish() {
        if (_handle and _handle.done()) {
            _ret.emplace(_handle.promise().move_return_value());
            _handle.destroy();
            _handle = {};
            return true;
        }
        return false;
    }

public:

    /// Advance to the next yielded value. Returns `true` if a new value is available.
    bool next() {
        if (not _handle) return false;
        if (_maybe_finish()) return false;
        _handle.resume();
        if (_maybe_finish()) return false;
        _count += 1;
        return true;
    }

    /// Advance to the next value (same as next()).
    Generator& operator++() {
        next();
        return *this;
    }

    /// Access the most recently yielded value.
    const T& operator*() const { return _handle.promise().value(); }
    /// @copydoc operator*() const
          T& operator*()       { return _handle.promise().value(); }

    /// Access the most recently yielded value via pointer.
    const T* operator->() const { return &_handle.promise().value(); }
    /// @copydoc operator->() const
          T* operator->()       { return &_handle.promise().value(); }

    /// Returns `true` if the generator has more values to produce.
    bool is_valid() const { return _handle and not _handle.done(); }
    /// @copydoc is_valid()
    operator bool() const { return is_valid(); }

    /// The number of values yielded so far.
    size_t count() const { return _count; }

    /// Returns `true` if the coroutine has finished (reached `co_return`).
    bool is_finished() const { return _ret.has_value(); }

    /// Access the coroutine's return value (only valid after is_finished() is true).
    ret_t& get_return_value()             { return *_ret; }
    /// @copydoc get_return_value()
    const ret_t& get_return_value() const { return *_ret; }

private:

    std::coroutine_handle<promise_type> _handle;
    std::optional<ret_t>                _ret;
    size_t _count = 0;

    Generator(std::coroutine_handle<promise_type> handle) : _handle(handle) {}

};


/**
 * @brief A wrapper around an optional Generator.
 *
 * If the inner generator is absent, MaybeGenerator behaves as an empty
 * (immediately exhausted) generator. This is useful in traversal adaptors
 * like prune_if(), where a node may or may not produce successors.
 *
 * @tparam G The generator type to wrap.
 */
template <typename G>
struct MaybeGenerator {
    std::optional<G> generator = std::nullopt;

    using Value = decltype(*std::declval<G>());

    MaybeGenerator() = default;

    MaybeGenerator(G&& gen): generator(std::move(gen)) {}
    MaybeGenerator(const G& gen): generator(gen) {}

    MaybeGenerator(const MaybeGenerator&) = default;
    MaybeGenerator(MaybeGenerator&&) = default;

    MaybeGenerator& operator=(const MaybeGenerator&) = default;
    MaybeGenerator& operator=(MaybeGenerator&&) = default;

    bool next() {
        if (not generator) return false;
        ++(*generator);
        return (bool) *generator;
    }

    MaybeGenerator& operator++() {
        if (generator) ++(*generator);
        return *this;
    }

    auto operator*() const { return **generator; }
    auto operator->() const { return (*generator).operator->(); }
    auto operator*() { return **generator; }
    auto operator->() { return (*generator).operator->(); }

    bool is_valid() const {
        if (not generator) return false;
        return (bool) *generator;
    }

    operator bool() const { return is_valid(); }
};


/**
 * @brief A generator that yields exactly one item, then exhausts.
 *
 * Satisfies `GeneratorConcept` and is useful for creating leaf-node
 * traversals with a single child.
 *
 * @tparam T The item type.
 */
template <typename T>
struct FixedItemGenerator {
    T    item;
    bool yielded = false;

    FixedItemGenerator(): yielded(true) {}
    FixedItemGenerator(T&& item): item(std::move(item)) {}
    FixedItemGenerator(const T& item): item(item) {}

    bool next() { yielded = true; return false; }

    FixedItemGenerator& operator++() { next(); return *this; }

    const T& operator*() const { return item; }
          T& operator*()       { return item; }
    const T* operator->() const { return &item; }
          T* operator->()       { return &item; }

    bool is_valid() const { return not yielded; }
    operator bool() const { return is_valid(); }
};


}  // namespace entttree
