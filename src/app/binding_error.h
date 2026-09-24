#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace app::bindings {

enum class ErrorCode {
    InvalidJson = 400,
    InvalidArgs = 400,
    MissingArg = 400,
    TypeMismatch = 400,
    InternalError = 500
};

class BindingError : public std::runtime_error {
  public:
    explicit BindingError(std::string message, ErrorCode code)
        : std::runtime_error(std::move(message)), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

  private:
    ErrorCode code_;
};

} // namespace app::bindings
