#include <hyprcursorfx/effect_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

namespace {
constexpr double PI = 3.14159265358979323846;
struct PulseState {
    HCFXColor color{0.41F, 0.84F, 1.0F, 0.95F};
    double radius = 30.0;
    int dotCount = 24;
    double dotSize = 3.5;
    double pulseSpeed = 1.6;
    double phase = 0.0;
    HCFXVec2 cursor{0.0, 0.0};
};

bool parseHex(const std::string &input, HCFXColor &out) {
    std::string s = input;
    if (!s.empty() && s.front() == '#') s.erase(s.begin());
    if (s.size() != 6) return false;
    unsigned int rgb = 0;
    if (std::sscanf(s.c_str(), "%x", &rgb) != 1) return false;
    out = {static_cast<float>((rgb >> 16) & 255u) / 255.0F,
           static_cast<float>((rgb >> 8) & 255u) / 255.0F,
           static_cast<float>(rgb & 255u) / 255.0F, 0.95F};
    return true;
}

void readSettings(PulseState &state, const char *path) {
    if (!path || !*path) return;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        try {
            if (key == "color") parseHex(value, state.color);
            else if (key == "radius") state.radius = std::clamp(std::stod(value), 8.0, 160.0);
            else if (key == "dot_count") state.dotCount = std::clamp(std::stoi(value), 4, 96);
            else if (key == "dot_size") state.dotSize = std::clamp(std::stod(value), 1.0, 12.0);
            else if (key == "pulse_speed") state.pulseSpeed = std::clamp(std::stod(value), 0.1, 8.0);
        } catch (...) {
            /* Keep the documented default for a malformed optional setting. */
        }
    }
}

void *create(const char *settingsPath) {
    auto *state = new PulseState;
    readSettings(*state, settingsPath);
    return state;
}
void destroy(void *opaque) { delete static_cast<PulseState *>(opaque); }
void pointerMove(void *opaque, HCFXVec2, HCFXVec2 to, uint64_t) {
    if (auto *state = static_cast<PulseState *>(opaque)) state->cursor = to;
}
void update(void *opaque, float dt, uint64_t, HCFXVec2 cursor) {
    auto *state = static_cast<PulseState *>(opaque);
    if (!state) return;
    state->cursor = cursor;
    state->phase = std::fmod(state->phase + std::max(0.0F, dt) * state->pulseSpeed, 2.0 * PI);
}
void render(void *opaque, const HCFXRenderContextV1 *ctx) {
    auto *state = static_cast<PulseState *>(opaque);
    if (!state || !ctx || ctx->abi_version != HCFX_EFFECT_API_VERSION) return;

    const double breathe = 0.5 + 0.5 * std::sin(state->phase);
    const double radius = state->radius * (0.82 + 0.18 * breathe);
    const float alpha = static_cast<float>(0.55 + 0.40 * breathe);
    HCFXColor color = state->color;
    color.a *= alpha;

    if (ctx->damage) ctx->damage(ctx->host, state->cursor.x, state->cursor.y, state->radius + 12.0);
    if (!ctx->circle) return;
    for (int i = 0; i < state->dotCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(state->dotCount);
        const double angle = t * 2.0 * PI + state->phase * 0.20;
        const double localPulse = 0.78 + 0.22 * std::sin(state->phase + t * 2.0 * PI);
        ctx->circle(ctx->host,
                    state->cursor.x + std::cos(angle) * radius,
                    state->cursor.y + std::sin(angle) * radius,
                    state->dotSize * localPulse,
                    color);
    }
}

const HCFXEffectDescriptorV1 DESCRIPTOR{
    HCFX_EFFECT_API_VERSION,
    sizeof(HCFXEffectDescriptorV1),
    "pulse",
    "Pulse Ring",
    "A configurable dotted ring that gently breathes around the cursor.",
    create,
    destroy,
    pointerMove,
    update,
    render,
};
} // namespace

extern "C" HCFX_EFFECT_EXPORT const HCFXEffectDescriptorV1 *hyprcursorfx_get_effect_v1(void) {
    return &DESCRIPTOR;
}
