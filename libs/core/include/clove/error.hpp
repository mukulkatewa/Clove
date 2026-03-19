#pragma once

#include <variant>
#include <string>
#include <cstdint>

namespace clove {

enum class ErrorCode : uint8_t {
    OK = 0,
    InvalidMessage,
    PermissionDenied,
    NotFound,
    Timeout,
    QuotaExceeded,
    PiiDetected,
    InternalError,
};

struct Error {
    ErrorCode code;
    std::string message;

    [[nodiscard]] bool is_ok() const noexcept { return code == ErrorCode::OK; }

    [[nodiscard]] const char* code_str() const noexcept {
        switch (code) {
            case ErrorCode::OK:               return "OK";
            case ErrorCode::InvalidMessage:   return "InvalidMessage";
            case ErrorCode::PermissionDenied: return "PermissionDenied";
            case ErrorCode::NotFound:         return "NotFound";
            case ErrorCode::Timeout:          return "Timeout";
            case ErrorCode::QuotaExceeded:    return "QuotaExceeded";
            case ErrorCode::PiiDetected:      return "PiiDetected";
            case ErrorCode::InternalError:    return "InternalError";
        }
        return "Unknown";
    }
};

template <typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error err) : data_(std::move(err)) {}

    [[nodiscard]] bool ok() const { return std::holds_alternative<T>(data_); }
    [[nodiscard]] explicit operator bool() const { return ok(); }

    [[nodiscard]] T& value() { return std::get<T>(data_); }
    [[nodiscard]] const T& value() const { return std::get<T>(data_); }
    [[nodiscard]] Error& error() { return std::get<Error>(data_); }
    [[nodiscard]] const Error& error() const { return std::get<Error>(data_); }

    [[nodiscard]] T& operator*() { return value(); }
    [[nodiscard]] const T& operator*() const { return value(); }

private:
    std::variant<T, Error> data_;
};

inline Error make_error(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

} // namespace clove
