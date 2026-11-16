#pragma once

#include <prism/net/http.hpp>

#include <glaze/json.hpp>

#include <expected>
#include <string>

// A response body read as JSON into a struct (requests' response.json(), typed): fields match by
// name, unknown keys are skipped. The JSON code is heavy, so it is a header of its own.
// T must have linkage (not in an anonymous namespace): clang cannot read its member names there.
namespace prism::net {

template <typename T>
std::expected<T, std::string> json(const Response& response) {
    if (!response.error.empty()) return std::unexpected(response.error);
    T value{};
    constexpr glz::opts opts{.error_on_unknown_keys = false};
    if (auto ec = glz::read<opts>(value, response.body)) return std::unexpected(glz::format_error(ec, response.body));
    return value;
}

} // namespace prism::net
