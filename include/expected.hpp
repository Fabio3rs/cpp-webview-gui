// expected.hpp - Compatibility wrapper for std::expected (C++23)
// Uses std::expected when available, otherwise provides a minimal
// implementation

#pragma once

#include <type_traits>
#include <utility>

// Feature detection for std::expected
#if __cplusplus >= 202302L ||                                                  \
    (defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L)
#define CLI_HAS_STD_EXPECTED 1
#include <expected>
#else
#define CLI_HAS_STD_EXPECTED 0
#endif

namespace cli {

#if CLI_HAS_STD_EXPECTED

// Use standard library implementation
template <typename T, typename E> using Expected = std::expected<T, E>;

template <typename E> using Unexpected = std::unexpected<E>;

template <typename E> [[nodiscard]] constexpr auto make_unexpected(E &&e) {
    return std::unexpected(std::forward<E>(e));
}

#else

// Minimal implementation for C++20

/// Tag type for unexpected value
template <typename E> class Unexpected {
  public:
    constexpr explicit Unexpected(const E &e) : error_(e) {}
    constexpr explicit Unexpected(E &&e) : error_(std::move(e)) {}

    [[nodiscard]] constexpr const E &error() const & noexcept { return error_; }
    [[nodiscard]] constexpr E &error() & noexcept { return error_; }
    [[nodiscard]] constexpr E &&error() && noexcept {
        return std::move(error_);
    }

  private:
    E error_;
};

// Deduction guide
template <typename E> Unexpected(E) -> Unexpected<E>;

/// Factory function for unexpected
template <typename E>
[[nodiscard]] constexpr Unexpected<std::decay_t<E>> make_unexpected(E &&e) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(e));
}

/// Minimal expected implementation
template <typename T, typename E> class Expected {
  public:
    using value_type = T;
    using error_type = E;

    // Default: has value (default-constructed)
    constexpr Expected()
        requires std::is_default_constructible_v<T>
        : has_value_(true), value_{} {}

    // Value constructor
    constexpr Expected(const T &v) : has_value_(true), value_(v) {}
    constexpr Expected(T &&v) : has_value_(true), value_(std::move(v)) {}

    // Unexpected constructor
    constexpr Expected(const Unexpected<E> &u)
        : has_value_(false), error_(u.error()) {}
    constexpr Expected(Unexpected<E> &&u)
        : has_value_(false), error_(std::move(u).error()) {}

    // Copy/move
    constexpr Expected(const Expected &) = default;
    constexpr Expected(Expected &&) = default;
    constexpr Expected &operator=(const Expected &) = default;
    constexpr Expected &operator=(Expected &&) = default;

    ~Expected() {
        if (has_value_) {
            value_.~T();
        } else {
            error_.~E();
        }
    }

    // Observers
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return has_value_;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return has_value_;
    }

    [[nodiscard]] constexpr const T &value() const & { return value_; }
    [[nodiscard]] constexpr T &value() & { return value_; }
    [[nodiscard]] constexpr T &&value() && { return std::move(value_); }

    [[nodiscard]] constexpr const E &error() const & { return error_; }
    [[nodiscard]] constexpr E &error() & { return error_; }
    [[nodiscard]] constexpr E &&error() && { return std::move(error_); }

    [[nodiscard]] constexpr const T &operator*() const & { return value_; }
    [[nodiscard]] constexpr T &operator*() & { return value_; }
    [[nodiscard]] constexpr T &&operator*() && { return std::move(value_); }

    [[nodiscard]] constexpr const T *operator->() const { return &value_; }
    [[nodiscard]] constexpr T *operator->() { return &value_; }

  private:
    bool has_value_;
    union {
        T value_;
        E error_;
    };
};

/// Specialization for void value type
template <typename E> class Expected<void, E> {
  public:
    using value_type = void;
    using error_type = E;

    // Default: success (no error)
    constexpr Expected() : has_value_(true) {}

    // Unexpected constructor
    constexpr Expected(const Unexpected<E> &u)
        : has_value_(false), error_(u.error()) {}
    constexpr Expected(Unexpected<E> &&u)
        : has_value_(false), error_(std::move(u).error()) {}

    // Copy/move
    constexpr Expected(const Expected &) = default;
    constexpr Expected(Expected &&) = default;
    constexpr Expected &operator=(const Expected &) = default;
    constexpr Expected &operator=(Expected &&) = default;

    ~Expected() {
        if (!has_value_) {
            error_.~E();
        }
    }

    // Observers
    [[nodiscard]] constexpr bool has_value() const noexcept {
        return has_value_;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return has_value_;
    }

    [[nodiscard]] constexpr const E &error() const & { return error_; }
    [[nodiscard]] constexpr E &error() & { return error_; }
    [[nodiscard]] constexpr E &&error() && { return std::move(error_); }

  private:
    bool has_value_;
    union {
        char dummy_; // For trivial default construction
        E error_;
    };
};

#endif // CLI_HAS_STD_EXPECTED

} // namespace cli
