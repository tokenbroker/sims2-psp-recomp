#pragma once

#include <filesystem>
#include <string>
#include <span>
#include <cstdint>

namespace psprecomp {

[[nodiscard]] std::string sha256_file(const std::filesystem::path &path);
[[nodiscard]] std::string sha256_bytes(std::span<const std::uint8_t> bytes);

} // namespace psprecomp
