#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

// Small dependency-free regression tests for the parser primitives used by the plugin.
struct Color { float r, g, b; };

static std::string trim(std::string value) {
    auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !space(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !space(c); }).base(), value.end());
    return value;
}

static std::optional<Color> parseHexColor(std::string value) {
    value = trim(std::move(value));
    if (!value.empty() && value.front() == '#') value.erase(value.begin());
    if (value.size() != 6) return std::nullopt;
    try {
        return Color{std::stoi(value.substr(0, 2), nullptr, 16) / 255.F,
                     std::stoi(value.substr(2, 2), nullptr, 16) / 255.F,
                     std::stoi(value.substr(4, 2), nullptr, 16) / 255.F};
    } catch (...) { return std::nullopt; }
}

int main() {
    assert(trim("  fairy_dust \t") == "fairy_dust");
    const auto pink = parseHexColor("#FF52C8");
    assert(pink.has_value());
    assert(std::fabs(pink->r - 1.F) < 0.001F);
    assert(std::fabs(pink->g - (82.F / 255.F)) < 0.001F);
    assert(!parseHexColor("#FFF").has_value());
    assert(!parseHexColor("garbage!").has_value());
    assert(parseHexColor(" 00FF7F ").has_value());
    return 0;
}
