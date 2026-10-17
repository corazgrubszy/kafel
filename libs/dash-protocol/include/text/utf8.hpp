#pragma once

#include <cstdint>
#include <string_view>

namespace kafel::utf8 {
struct Character { char32_t codepoint; bool valid; };

// Invalid sequences consume one byte, allowing static text to recover safely.
inline Character decode(std::string_view& input) noexcept {
    if (input.empty()) return {0, false};
    const auto first = static_cast<unsigned char>(input.front());
    const auto invalid = [&]() { input.remove_prefix(1); return Character{U'\ufffd', false}; };
    if (first < 0x80) { input.remove_prefix(1); return {first, true}; }
    unsigned length;
    char32_t value, minimum;
    if (first >= 0xc2 && first <= 0xdf) { length = 2; value = first & 31; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { length = 3; value = first & 15; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { length = 4; value = first & 7; minimum = 0x10000; }
    else return invalid();
    if (input.size() < length) return invalid();
    for (unsigned i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(input[i]);
        if ((byte & 0xc0) != 0x80) return invalid();
        value = (value << 6) | (byte & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return invalid();
    input.remove_prefix(length);
    return {value, true};
}

// Small, explicit Polish composition table, not a general Unicode normalizer.
inline char32_t compose(char32_t base, char32_t mark) noexcept {
    if (mark == 0x301) {
        switch (base) {
        case U'c': return U'ć'; case U'C': return U'Ć';
        case U'n': return U'ń'; case U'N': return U'Ń';
        case U'o': return U'ó'; case U'O': return U'Ó';
        case U's': return U'ś'; case U'S': return U'Ś';
        case U'z': return U'ź'; case U'Z': return U'Ź';
        }
    }
    if (mark == 0x307) {
        if (base == U'z') return U'ż';
        if (base == U'Z') return U'Ż';
    }
    if (mark == 0x328) {
        switch (base) {
        case U'a': return U'ą'; case U'A': return U'Ą';
        case U'e': return U'ę'; case U'E': return U'Ę';
        }
    }
    return 0;
}

inline Character next(std::string_view& input) noexcept {
    const auto current = decode(input);
    if (!current.valid || input.empty()) return current;
    auto rest = input;
    const auto mark = decode(rest);
    if (mark.valid) {
        if (const auto combined = compose(current.codepoint, mark.codepoint)) {
            input = rest;
            return {combined, true};
        }
    }
    return current;
}
} // namespace kafel::utf8
