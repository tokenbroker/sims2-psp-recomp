#pragma once
#include <string_view>
namespace sims2::display {
inline std::string_view save_slot_label(std::string_view slot) noexcept {
    return slot.empty()?std::string_view("Default save"):slot;
}
}
