#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sims2::display {
// Routing only: the selected runtime separately verifies the executable hash.
inline bool european_disc(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open the selected ISO");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 17 * 2048) throw std::runtime_error("Truncated ISO");
    const auto read = [&](std::uint64_t offset, std::uint8_t *out, std::size_t count) {
        if (offset > std::uint64_t(length) || count > std::uint64_t(length) - offset)
            throw std::runtime_error("ISO extent outside the file");
        input.seekg(static_cast<std::streamoff>(offset));
        input.read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(count));
        if (!input) throw std::runtime_error("Cannot read the selected ISO");
    };
    const auto le32 = [](const std::uint8_t *p) {
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
               (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    };
    std::array<std::uint8_t, 2048> pvd{};
    read(16 * 2048, pvd.data(), pvd.size());
    if (pvd[0] != 1 || std::string(reinterpret_cast<char *>(pvd.data() + 1), 5) != "CD001" ||
        pvd[6] != 1 || pvd[156] < 34)
        throw std::runtime_error("Expected a plain ISO9660 PSP disc");
    const auto size = le32(pvd.data() + 166);
    if (!size || size > 16 * 1024 * 1024) throw std::runtime_error("Invalid ISO root directory");
    std::vector<std::uint8_t> root(size);
    read(std::uint64_t(le32(pvd.data() + 158)) * 2048, root.data(), root.size());
    for (std::size_t cursor = 0; cursor < root.size();) {
        const auto count = root[cursor];
        if (!count) { cursor = (cursor / 2048 + 1) * 2048; continue; }
        if (count < 34 || count > root.size() - cursor || 33u + root[cursor + 32] > count)
            throw std::runtime_error("Invalid ISO root record");
        const auto *record = root.data() + cursor;
        std::string name(reinterpret_cast<const char *>(record + 33), record[32]);
        name = name.substr(0, name.find(';'));
        if (name == "UMD_DATA.BIN" && !(record[25] & 2)) {
            const auto bytes = le32(record + 10);
            if (!bytes || bytes > 4096) throw std::runtime_error("Invalid PSP disc identity size");
            std::vector<std::uint8_t> identity(bytes);
            read(std::uint64_t(le32(record + 2)) * 2048, identity.data(), identity.size());
            const std::string id(identity.begin(), identity.end());
            if (id.starts_with("ULUS-10031|")) return false;
            if (id.starts_with("ULES-00211|")) return true;
            throw std::runtime_error("Unsupported disc; use ULUS-10031 USA or ULES-00211 Europe");
        }
        cursor += count;
    }
    throw std::runtime_error("Missing PSP disc identity");
}
}
