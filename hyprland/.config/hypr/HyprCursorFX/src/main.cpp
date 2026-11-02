#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace {
    constexpr std::string_view VERSION = "0.4.0";
    constexpr size_t HARD_MAX_PARTICLES = 420;
    constexpr size_t THOLMAN_TRAILING_POINTS = 15;
    constexpr size_t THOLMAN_RAINBOW_POINTS = 20;
    constexpr float THOLMAN_CLOCK_RADIUS = 45.F;
    constexpr float THOLMAN_CLOCK_FOLLOW_RATE = 0.4F;

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    enum class EffectMode {
        Off,
        FairyDust,
        Ghost,
        Trailing,
        Bubbles,
        Snowflakes,
        Rainbow,
        FollowingDot,
        Clock,
    };

    struct Color {
        float r = 1.F;
        float g = 1.F;
        float b = 1.F;
    };

    // Exact palette used by tholman/cursor-effects/src/rainbowCursor.js.
    constexpr std::array<Color, 6> THOLMAN_RAINBOW_COLORS = {{{254.F / 255.F, 0.F, 0.F}, {253.F / 255.F, 140.F / 255.F, 0.F},
        {1.F, 229.F / 255.F, 0.F}, {17.F / 255.F, 159.F / 255.F, 11.F / 255.F},
        {6.F / 255.F, 68.F / 255.F, 179.F / 255.F}, {194.F / 255.F, 46.F / 255.F, 220.F / 255.F}}};

    struct Settings {
        EffectMode mode = EffectMode::FairyDust;
        bool enabled = true;
        int maxParticles = 240;
        int lifetimeMs = 1200;
        int spawnSpacing = 2; // legacy general spacing; site-compatible modes use their own thresholds
        int particleSize = 4;
        int ghostCount = 15;
        int trailLifetimeMs = 667;
        int bubbleSpacing = 5;
        int bubbleLimit = 100;
        int bubbleLifetimeMs = 4200;
        int fairyDustSpacing = 2;
        int fairyDustLimit = 140;
        int snowflakeSpacing = 3;
        int snowflakeLimit = 32;
        int ghostSpacing = 5;
        int ghostLimit = 18;
        int rainbowSize = 3;
        float clockRadius = THOLMAN_CLOCK_RADIUS;
        float speed = 1.F;
        Color bubbleFill{0.902F, 0.945F, 0.969F};
        Color bubbleStroke{0.227F, 0.573F, 0.773F};
        Color followingDot{0.196F, 0.196F, 0.196F};
        float followingDotAlpha = 0.65F;
        Color clockDateColor{0.F, 0.F, 1.F};
        Color clockFaceColor{0.F, 0.F, 0.F};
        Color clockSecondsColor{1.F, 0.F, 0.F};
        Color clockMinutesColor{0.F, 0.F, 0.F};
        Color clockHoursColor{0.F, 0.F, 0.F};
        std::vector<Color> colors = {
            {0.839F, 0.110F, 0.349F}, // #D61C59
            {0.906F, 0.847F, 0.294F}, // #E7D84B
            {0.106F, 0.529F, 0.596F}, // #1B8798
        };
    };

    struct Particle {
        Vector2D origin;
        Vector2D velocity; // source-like velocity in logical pixels per 60 Hz frame
        TimePoint born;
        int lifetimeMs = 1200;
        float size = 4.F;
        float opacity = 1.F;
        float phase = 0.F;
        float acceleration = 0.F;
        Color color;
        EffectMode type = EffectMode::FairyDust;
        char glyph = '*';
    };

    HANDLE g_handle = nullptr;
    Settings g_settings;
    std::vector<Particle> g_particles;
    std::vector<Vector2D> g_cursorTail;
    Vector2D g_followDotPos{};
    Vector2D g_lastCursorPos{};
    Vector2D g_lastBubbleSpawn{};
    Vector2D g_lastFairyDustSpawn{};
    Vector2D g_lastSnowflakeSpawn{};
    Vector2D g_lastGhostSpawn{};
    TimePoint g_lastTailUpdate{};
    TimePoint g_lastClockUpdate{};
    std::string g_clockDateText;
    std::vector<Vector2D> g_clockNodes;
    bool g_haveLastCursorPos = false;
    bool g_haveBubbleSpawn = false;
    bool g_haveFairyDustSpawn = false;
    bool g_haveSnowflakeSpawn = false;
    bool g_haveGhostSpawn = false;
    EffectMode g_previousMode = EffectMode::FairyDust;
    std::mt19937 g_rng{std::random_device{}()};
    uint64_t g_mouseMoveEvents = 0;
    uint64_t g_renderStageEvents = 0;
    uint64_t g_lastMomentCallbacks = 0;
    uint64_t g_activeRenderFrames = 0;
    uint64_t g_missingRendererFrames = 0;
    uint64_t g_missingMonitorFrames = 0;
    uint64_t g_cursorMonitorFallbackFrames = 0;
    uint64_t g_queuedEffectElements = 0;

    std::string makeClockDateText(std::time_t timestamp);

    using MouseListener = decltype(Event::bus()->m_events.input.mouse.move.listen(
        std::function<void(Vector2D, Event::SCallbackInfo&)>{}));
    using RenderListener = decltype(Event::bus()->m_events.render.stage.listen(
        std::function<void(eRenderStage)>{}));
    std::optional<MouseListener> g_mouseListener;
    std::optional<RenderListener> g_renderListener;

    std::string trim(std::string value) {
        const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !isSpace(c); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !isSpace(c); }).base(), value.end());
        return value;
    }

    std::string lower(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::filesystem::path configPath() {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg)
            return std::filesystem::path{xdg} / "hypr" / "hyprcursorfx.conf";
        const char* home = std::getenv("HOME");
        if (home && *home)
            return std::filesystem::path{home} / ".config" / "hypr" / "hyprcursorfx.conf";
        return std::filesystem::path{"hyprcursorfx.conf"};
    }

    EffectMode parseMode(std::string value, bool* ok = nullptr) {
        value = lower(trim(std::move(value)));
        if (ok)
            *ok = true;
        if (value == "off" || value == "none") return EffectMode::Off;
        if (value == "fairy_dust" || value == "fairy-dust" || value == "fairy") return EffectMode::FairyDust;
        if (value == "ghost" || value == "ghost_following" || value == "ghost-following") return EffectMode::Ghost;
        if (value == "trailing" || value == "trail") return EffectMode::Trailing;
        if (value == "bubbles" || value == "bubble") return EffectMode::Bubbles;
        if (value == "snowflakes" || value == "snowflake" || value == "snow") return EffectMode::Snowflakes;
        if (value == "rainbow" || value == "rainbow_cursor" || value == "rainbow-cursor") return EffectMode::Rainbow;
        if (value == "following_dot" || value == "following-dot" || value == "dot") return EffectMode::FollowingDot;
        if (value == "clock" || value == "time") return EffectMode::Clock;
        if (ok)
            *ok = false;
        return EffectMode::Off;
    }

    std::string modeName(EffectMode mode) {
        switch (mode) {
            case EffectMode::Off: return "off";
            case EffectMode::FairyDust: return "fairy_dust";
            case EffectMode::Ghost: return "ghost";
            case EffectMode::Trailing: return "trailing";
            case EffectMode::Bubbles: return "bubbles";
            case EffectMode::Snowflakes: return "snowflakes";
            case EffectMode::Rainbow: return "rainbow";
            case EffectMode::FollowingDot: return "following_dot";
            case EffectMode::Clock: return "clock";
        }
        return "off";
    }

    bool parseBool(std::string value, bool fallback) {
        value = lower(trim(std::move(value)));
        if (value == "true" || value == "yes" || value == "1" || value == "on") return true;
        if (value == "false" || value == "no" || value == "0" || value == "off") return false;
        return fallback;
    }

    int parseInt(std::string value, int fallback, int low, int high) {
        try {
            size_t consumed = 0;
            const int parsed = std::stoi(trim(std::move(value)), &consumed);
            (void)consumed;
            return std::clamp(parsed, low, high);
        } catch (...) {
            return fallback;
        }
    }

    float parseFloat(std::string value, float fallback, float low, float high) {
        try {
            size_t consumed = 0;
            const float parsed = std::stof(trim(std::move(value)), &consumed);
            (void)consumed;
            if (!std::isfinite(parsed))
                return fallback;
            return std::clamp(parsed, low, high);
        } catch (...) {
            return fallback;
        }
    }

    std::optional<Color> parseHexColor(std::string value) {
        value = trim(std::move(value));
        if (!value.empty() && value.front() == '#')
            value.erase(value.begin());
        if (value.size() != 6)
            return std::nullopt;
        try {
            const auto r = std::stoi(value.substr(0, 2), nullptr, 16);
            const auto g = std::stoi(value.substr(2, 2), nullptr, 16);
            const auto b = std::stoi(value.substr(4, 2), nullptr, 16);
            return Color{r / 255.F, g / 255.F, b / 255.F};
        } catch (...) {
            return std::nullopt;
        }
    }

    std::vector<Color> parseColors(std::string value, const std::vector<Color>& fallback) {
        std::vector<Color> parsed;
        std::stringstream stream(value);
        std::string item;
        while (std::getline(stream, item, ',')) {
            if (auto color = parseHexColor(item))
                parsed.push_back(*color);
            if (parsed.size() >= 12)
                break;
        }
        return parsed.empty() ? fallback : parsed;
    }

    bool readSettings(Settings& out, std::string& error) {
        const auto path = configPath();
        std::ifstream file(path);
        if (!file) {
            error = "Config not found; using built-in defaults: " + path.string();
            return false;
        }

        Settings next;
        std::string line;
        while (std::getline(file, line)) {
            line = trim(std::move(line));
            if (line.empty() || line.starts_with(';') || line.starts_with("//") || line.starts_with('#'))
                continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = lower(trim(line.substr(0, eq)));
            const std::string value = trim(line.substr(eq + 1));
            if (key == "mode") {
                bool valid = false;
                const auto mode = parseMode(value, &valid);
                if (valid)
                    next.mode = mode;
            } else if (key == "enabled") {
                next.enabled = parseBool(value, next.enabled);
            } else if (key == "max_particles") {
                next.maxParticles = parseInt(value, next.maxParticles, 1, static_cast<int>(HARD_MAX_PARTICLES));
            } else if (key == "lifetime_ms") {
                next.lifetimeMs = parseInt(value, next.lifetimeMs, 100, 5000);
            } else if (key == "spawn_spacing") {
                next.spawnSpacing = parseInt(value, next.spawnSpacing, 2, 48);
            } else if (key == "particle_size") {
                next.particleSize = parseInt(value, next.particleSize, 1, 24);
            } else if (key == "ghost_count") {
                next.ghostCount = parseInt(value, next.ghostCount, 1, 12);
            } else if (key == "trail_lifetime_ms") {
                next.trailLifetimeMs = parseInt(value, next.trailLifetimeMs, 100, 2500);
            } else if (key == "bubble_spacing") {
                next.bubbleSpacing = parseInt(value, next.bubbleSpacing, 2, 80);
            } else if (key == "bubble_limit") {
                next.bubbleLimit = parseInt(value, next.bubbleLimit, 4, 140);
            } else if (key == "bubble_lifetime_ms") {
                next.bubbleLifetimeMs = parseInt(value, next.bubbleLifetimeMs, 1000, 8000);
            } else if (key == "fairy_dust_spacing") {
                next.fairyDustSpacing = parseInt(value, next.fairyDustSpacing, 1, 16);
            } else if (key == "fairy_dust_limit") {
                next.fairyDustLimit = parseInt(value, next.fairyDustLimit, 8, 180);
            } else if (key == "snowflake_spacing") {
                next.snowflakeSpacing = parseInt(value, next.snowflakeSpacing, 2, 40);
            } else if (key == "snowflake_limit") {
                next.snowflakeLimit = parseInt(value, next.snowflakeLimit, 4, 64);
            } else if (key == "ghost_spacing") {
                next.ghostSpacing = parseInt(value, next.ghostSpacing, 2, 24);
            } else if (key == "ghost_limit") {
                next.ghostLimit = parseInt(value, next.ghostLimit, 4, 36);
            } else if (key == "rainbow_size") {
                next.rainbowSize = parseInt(value, next.rainbowSize, 1, 6);
            } else if (key == "clock_radius") {
                next.clockRadius = parseFloat(value, next.clockRadius, 20.F, 100.F);
            } else if (key == "bubble_fill") {
                if (auto color = parseHexColor(value)) next.bubbleFill = *color;
            } else if (key == "bubble_stroke") {
                if (auto color = parseHexColor(value)) next.bubbleStroke = *color;
            } else if (key == "following_dot_color") {
                if (auto color = parseHexColor(value)) next.followingDot = *color;
            } else if (key == "following_dot_alpha") {
                next.followingDotAlpha = parseFloat(value, next.followingDotAlpha, 0.1F, 1.F);
            } else if (key == "clock_date_color") {
                if (auto color = parseHexColor(value)) next.clockDateColor = *color;
            } else if (key == "clock_face_color") {
                if (auto color = parseHexColor(value)) next.clockFaceColor = *color;
            } else if (key == "clock_seconds_color") {
                if (auto color = parseHexColor(value)) next.clockSecondsColor = *color;
            } else if (key == "clock_minutes_color") {
                if (auto color = parseHexColor(value)) next.clockMinutesColor = *color;
            } else if (key == "clock_hours_color") {
                if (auto color = parseHexColor(value)) next.clockHoursColor = *color;
            } else if (key == "speed") {
                next.speed = parseFloat(value, next.speed, 0.1F, 4.F);
            } else if (key == "colors") {
                next.colors = parseColors(value, next.colors);
            }
        }
        if (!next.enabled)
            next.mode = EffectMode::Off;
        out = std::move(next);
        error.clear();
        return true;
    }

    float randomFloat(float low, float high) {
        std::uniform_real_distribution<float> distribution(low, high);
        return distribution(g_rng);
    }

    int randomInt(int low, int high) {
        std::uniform_int_distribution<int> distribution(low, high);
        return distribution(g_rng);
    }

    Color colorFor(size_t index) {
        if (g_settings.colors.empty())
            return {1.F, 1.F, 1.F};
        return g_settings.colors[index % g_settings.colors.size()];
    }

    Vector2D lerp(const Vector2D& a, const Vector2D& b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }

    float distance(const Vector2D& a, const Vector2D& b) {
        const float dx = static_cast<float>(a.x - b.x);
        const float dy = static_cast<float>(a.y - b.y);
        return std::sqrt(dx * dx + dy * dy);
    }

    void scheduleDamage(PHLMONITOR monitorHint, const Vector2D& globalPos, float radius) {
        if (!g_pHyprRenderer)
            return;

        // CMonitor::addDamage(CBox) consumes monitor-local, scaled pixel
        // coordinates. This is the same conversion IHyprRenderer::damageBox()
        // performs before forwarding damage to a monitor. Passing global logical
        // coordinates here can damage the wrong area and clip every effect out.
        auto targetMonitor = State::monitorState()->query().vec(globalPos).run();
        if (!targetMonitor)
            targetMonitor = monitorHint; // keep edge particles alive at monitor borders
        if (!targetMonitor)
            return;

        const double scale = targetMonitor->m_scale;
        const Vector2D local = globalPos - targetMonitor->m_position;
        const double r = std::max(static_cast<double>(radius), 2.0) * scale;
        CBox damage{std::floor(local.x * scale - r),
                    std::floor(local.y * scale - r),
                    std::ceil(r * 2.0),
                    std::ceil(r * 2.0)};
        targetMonitor->addDamage(damage);
    }

    bool isNearMonitor(PHLMONITOR monitor, const Vector2D& globalPos, float margin = 64.F) {
        if (!monitor)
            return false;
        const auto local = globalPos - monitor->m_position;
        return local.x >= -margin && local.y >= -margin &&
               local.x <= monitor->m_size.x + margin && local.y <= monitor->m_size.y + margin;
    }

    void addParticle(const Vector2D& origin, const Vector2D& velocity, float size, float opacity,
                     EffectMode type, size_t colorIndex, int lifetimeMs, float phase = 0.F,
                     float acceleration = 0.F, char glyph = '*') {
        size_t perEffectLimit = static_cast<size_t>(g_settings.maxParticles);
        switch (type) {
            case EffectMode::Bubbles: perEffectLimit = static_cast<size_t>(g_settings.bubbleLimit); break;
            case EffectMode::FairyDust: perEffectLimit = static_cast<size_t>(g_settings.fairyDustLimit); break;
            case EffectMode::Snowflakes: perEffectLimit = static_cast<size_t>(g_settings.snowflakeLimit); break;
            case EffectMode::Ghost: perEffectLimit = static_cast<size_t>(g_settings.ghostLimit); break;
            default: break;
        }

        const auto sameType = static_cast<size_t>(std::count_if(g_particles.begin(), g_particles.end(),
            [&](const Particle& particle) { return particle.type == type; }));
        if (sameType >= perEffectLimit) {
            const auto oldest = std::find_if(g_particles.begin(), g_particles.end(),
                [&](const Particle& particle) { return particle.type == type; });
            if (oldest != g_particles.end())
                g_particles.erase(oldest);
        }
        if (g_particles.size() >= static_cast<size_t>(g_settings.maxParticles) || g_particles.size() >= HARD_MAX_PARTICLES)
            g_particles.erase(g_particles.begin());

        g_particles.push_back(Particle{
            .origin = origin,
            .velocity = velocity,
            .born = Clock::now(),
            .lifetimeMs = lifetimeMs,
            .size = size,
            .opacity = opacity,
            .phase = phase,
            .acceleration = acceleration,
            .color = colorFor(colorIndex),
            .type = type,
            .glyph = glyph,
        });
    }

    void emitForMotion(const Vector2D& from, const Vector2D& to) {
        const float travelled = distance(from, to);
        if (travelled < 0.01F)
            return;

        // These are native ports of the matching effects from tholman/cursor-effects.
        // Their browser canvas update is 60 Hz; velocities here remain in pixels/frame
        // and are evaluated analytically from particle age to keep the plugin frame-rate independent.
        if (g_settings.mode == EffectMode::FairyDust) {
            // Match the upstream sampling rule: compare against the last emitted
            // sparkle, not the previous high-frequency pointer event. Otherwise
            // subpixel events continually reset the distance and no sparkles emit.
            if (!g_haveFairyDustSpawn) {
                g_lastFairyDustSpawn = from;
                g_haveFairyDustSpawn = true;
            }
            const float spacing = static_cast<float>(g_settings.fairyDustSpacing);
            Vector2D sample = g_lastFairyDustSpawn;
            int spawnCount = 0;
            while (distance(sample, to) >= spacing && spawnCount < 16) {
                const float remainingDistance = distance(sample, to);
                const Vector2D direction{(to.x - sample.x) / remainingDistance,
                                          (to.y - sample.y) / remainingDistance};
                sample = sample + direction * spacing;
                const float sign = randomFloat(0.F, 1.F) < 0.5F ? -1.F : 1.F;
                const Vector2D velocity{sign * randomFloat(0.F, 0.5F) * g_settings.speed,
                                        randomFloat(0.9F, 1.6F) * g_settings.speed};
                const int frames = randomInt(60, 89);
                const size_t colorIndex = static_cast<size_t>(randomInt(0, static_cast<int>(g_settings.colors.size()) - 1));
                addParticle(sample, velocity, randomFloat(8.0F, 10.5F), 1.F, EffectMode::FairyDust,
                            colorIndex, static_cast<int>(std::lround(frames * (1000.F / 60.F))),
                            randomFloat(0.F, 6.28318F), 0.F, '*');
                ++spawnCount;
            }
            g_lastFairyDustSpawn = sample;
        } else if (g_settings.mode == EffectMode::Bubbles) {
            const bool spaced = !g_haveBubbleSpawn || distance(g_lastBubbleSpawn, to) >= static_cast<float>(g_settings.bubbleSpacing);
            if (spaced) {
                const float sign = randomFloat(0.F, 1.F) < 0.5F ? -1.F : 1.F;
                const Vector2D velocity{sign * randomFloat(0.F, 0.1F) * g_settings.speed,
                                        (-0.4F - randomFloat(0.F, 1.F)) * g_settings.speed};
                const int lowerLife = std::max(60, g_settings.bubbleLifetimeMs * 4 / 5 * 60 / 1000);
                const int upperLife = std::max(lowerLife + 1, g_settings.bubbleLifetimeMs * 6 / 5 * 60 / 1000);
                const int frames = randomInt(lowerLife, upperLife);
                addParticle(to, velocity, 4.F, 1.F, EffectMode::Bubbles, 0,
                            static_cast<int>(std::lround(frames * (1000.F / 60.F))), randomFloat(0.F, 6.28318F),
                            randomFloat(0.F, 1.F) / 1200.F);
                g_lastBubbleSpawn = to;
                g_haveBubbleSpawn = true;
            }
        } else if (g_settings.mode == EffectMode::Snowflakes) {
            const bool spaced = !g_haveSnowflakeSpawn || distance(g_lastSnowflakeSpawn, to) >= static_cast<float>(g_settings.snowflakeSpacing);
            if (spaced) {
                const float sign = randomFloat(0.F, 1.F) < 0.5F ? -1.F : 1.F;
                const Vector2D velocity{sign * randomFloat(0.15F, 0.55F) * g_settings.speed,
                                        (3.0F + randomFloat(0.F, 1.4F)) * g_settings.speed};
                const int frames = randomInt(48, 78); // fast ASCII flakes, about 0.8–1.3 s
                constexpr std::array<char, 3> glyphs{'*', '+', 'x'};
                const char glyph = glyphs[static_cast<size_t>(randomInt(0, static_cast<int>(glyphs.size()) - 1))];
                addParticle(to, velocity, randomFloat(9.F, 11.F), 1.F, EffectMode::Snowflakes, 0,
                            static_cast<int>(std::lround(frames * (1000.F / 60.F))), randomFloat(0.F, 6.28318F),
                            randomFloat(0.0007F, 0.0015F), glyph);
                g_lastSnowflakeSpawn = to;
                g_haveSnowflakeSpawn = true;
            }
        } else if (g_settings.mode == EffectMode::Ghost) {
            const bool spaced = !g_haveGhostSpawn || distance(g_lastGhostSpawn, to) >= static_cast<float>(g_settings.ghostSpacing);
            if (spaced) {
                // The original ghost cursor keeps each cursor sprite for about 40 animation frames.
                addParticle(to, Vector2D{0.0, 0.0}, 1.F, 1.F, EffectMode::Ghost, 0, 667);
                g_lastGhostSpawn = to;
                g_haveGhostSpawn = true;
            }
        }
    }

    void seedEffectAt(const Vector2D& pos) {
        g_lastCursorPos = pos;
        g_haveLastCursorPos = true;
        g_lastBubbleSpawn = pos;
        g_lastFairyDustSpawn = pos;
        g_lastSnowflakeSpawn = pos;
        g_lastGhostSpawn = pos;
        g_haveBubbleSpawn = false;
        g_haveFairyDustSpawn = false;
        g_haveSnowflakeSpawn = false;
        g_haveGhostSpawn = false;
        g_followDotPos = pos;
        g_lastTailUpdate = TimePoint{};
        g_lastClockUpdate = TimePoint{};
        g_clockNodes.clear();
        g_clockDateText.clear();
        g_cursorTail.clear();
        if (g_settings.mode == EffectMode::Trailing)
            g_cursorTail.assign(THOLMAN_TRAILING_POINTS, pos);
        else if (g_settings.mode == EffectMode::Rainbow)
            g_cursorTail.assign(THOLMAN_RAINBOW_POINTS, pos);
        if (g_settings.mode == EffectMode::Clock) {
            g_clockDateText = makeClockDateText(std::time(nullptr));
            const size_t nodeCount = g_clockDateText.size() + 12 + 3 + 4 + 5;
            g_clockNodes.assign(nodeCount, pos);
        }

        if (auto monitor = State::monitorState()->query().vec(pos).run())
            scheduleDamage(monitor, pos, 32.F);
    }

    void onMouseMove(Vector2D eventPos, Event::SCallbackInfo&) {
        ++g_mouseMoveEvents;
        if (!g_handle || !g_settings.enabled || g_settings.mode == EffectMode::Off)
            return;

        // Use the event's global logical coordinates; Pointer::mgr() may still be one frame behind.
        const Vector2D current = eventPos;
        if (!g_haveLastCursorPos) {
            seedEffectAt(current);
            return;
        }

        const Vector2D previous = g_lastCursorPos;
        g_lastCursorPos = current;
        emitForMotion(previous, current);

        if (auto monitor = State::monitorState()->query().vec(current).run()) {
            scheduleDamage(monitor, previous, 36.F);
            scheduleDamage(monitor, current, 36.F);
        }
    }

    void addRect(PHLMONITOR monitor, const Vector2D& globalCenter, float width, float height,
                 const Color& color, float alpha, bool rounded = true) {
        if (!monitor || !g_pHyprRenderer)
            return;
        const auto local = globalCenter - monitor->m_position;
        const float scale = static_cast<float>(monitor->m_scale);
        CRectPassElement::SRectData data{};
        data.box = CBox{(local.x - width * 0.5) * scale,
                        (local.y - height * 0.5) * scale,
                        width * scale,
                        height * scale};
        data.color = CHyprColor{color.r, color.g, color.b, std::clamp(alpha, 0.F, 1.F)};
        data.round = rounded ? std::max(1, static_cast<int>(std::ceil(std::min(width, height) * scale * 0.5F))) : 0;
        // In Hyprland 0.56.2 RENDER_LAST_MOMENT is emitted immediately BEFORE
        // endRender(). endRender() still executes m_renderPass.render(), so an
        // immediate draw() here is overwritten by the queued window pass.
        // Append the effect to the current pass; it will render after windows,
        // above the desktop, and before the frame is submitted.
        g_pHyprRenderer->addPassElement(Hyprutils::Memory::makeUnique<CRectPassElement>(data));
        ++g_queuedEffectElements;
    }

    void addDot(PHLMONITOR monitor, const Vector2D& center, float diameter, const Color& color, float alpha) {
        const float d = std::max(1.0F, diameter);
        addRect(monitor, center, d, d, color, alpha, true);
    }

    void drawBubble(PHLMONITOR monitor, const Vector2D& cursorOrigin, float progress, float opacity) {
        // Port of bubbleCursor.js: arc radius is baseDimension(4) * scale,
        // scale goes from 0.2 to 1.2 over 60-119 frames. The original arc's
        // centre is offset left by 2*scale and upward by 2px from the event.
        const float scale = 0.2F + std::clamp(progress, 0.F, 1.F);
        const float radius = 4.F * scale;
        const float diameter = radius * 2.F;
        const Vector2D center{cursorOrigin.x - 2.F * scale, cursorOrigin.y - 2.F};

        // Use two fully-rounded circles rather than drawing a rounded rectangle
        // with a circle-like border radius. addRect rounds UP to the true radius,
        // so this remains circular at fractional output scales too.
        addDot(monitor, center, diameter, g_settings.bubbleStroke, 0.98F * opacity);
        addDot(monitor, center, std::max(0.8F, diameter - 1.35F), g_settings.bubbleFill, 0.97F * opacity);
        if (radius > 2.5F) {
            addDot(monitor, center + Vector2D{-radius * 0.28, -radius * 0.28},
                   0.9F, {1.F, 1.F, 1.F}, 0.70F * opacity);
        }
    }

    void addSegmentDots(PHLMONITOR monitor, const Vector2D& a, const Vector2D& b,
                        float diameter, float spacing, const Color& color, float alpha) {
        const float length = distance(a, b);
        const int steps = std::clamp(static_cast<int>(std::ceil(length / std::max(0.75F, spacing))), 1, 256);
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            addDot(monitor, lerp(a, b, t), diameter, color, alpha);
        }
    }

    Vector2D particlePosition(const Particle& particle, float ageSeconds) {
        const float frames = std::max(0.F, ageSeconds) * 60.F;
        Vector2D pos = particle.origin + particle.velocity * frames;
        if (particle.type == EffectMode::FairyDust)
            pos.y += 0.01 * frames * frames; // source's vy += 0.02 per 60 Hz frame
        else if (particle.type == EffectMode::Bubbles)
            pos.y -= particle.acceleration * frames * frames;
        else if (particle.type == EffectMode::Snowflakes) {
            pos.y += particle.acceleration * frames * frames;
            pos.x += std::sin(particle.phase + ageSeconds * 5.0F) * std::min(3.2F, ageSeconds * 2.5F);
        }
        if (particle.type == EffectMode::Bubbles) {
            // The original applies a small random walk to vx each frame. A bounded
            // deterministic wobble keeps the same subtle drift without frame-rate
            // dependent random integration or large sideways swings.
            const float wobble = std::min(2.2F, ageSeconds * 1.8F);
            pos.x += std::sin(particle.phase + ageSeconds * 3.8F) * wobble;
        }
        return pos;
    }

    void drawAsciiGlyph(PHLMONITOR monitor, const Vector2D& pos, float size, char glyph,
                        const Color& color, float alpha) {
        // Rasterize a compact 5x7 ASCII-like glyph into the current Hyprland render pass.
        // No font/emoji dependency: each lit bitmap cell is a crisp tiny rectangle.
        if (size < 3.8F || alpha < 0.015F)
            return;

        using Bitmap = std::array<std::string_view, 7>;
        Bitmap rows{};
        switch (glyph) {
            case '+': rows = {"..*..", "..*..", "..*..", "*****", "..*..", "..*..", "..*.."}; break;
            case 'x': rows = {"*...*", ".*.*.", "..*..", ".....", "..*..", ".*.*.", "*...*"}; break;
            case 'o': rows = {".***.", "*...*", "*...*", "*...*", "*...*", "*...*", ".***."}; break;
            case '.': rows = {".....", ".....", ".....", ".....", ".....", "..*..", "..*.."}; break;
            case '*':
            default: rows = {"..*..", "*.*.*", "..*..", "*****", "..*..", "*.*.*", "..*.."}; break;
        }

        const float cell = std::clamp(size / 7.F, 0.55F, 1.55F);
        const float pixel = cell * 0.88F;
        for (size_t row = 0; row < rows.size(); ++row) {
            for (size_t col = 0; col < rows[row].size(); ++col) {
                if (rows[row][col] != '*')
                    continue;
                const Vector2D offset{(static_cast<double>(col) - 2.0) * cell,
                                      (static_cast<double>(row) - 3.0) * cell};
                addRect(monitor, pos + offset, pixel, pixel, color, alpha, false);
            }
        }
    }

    std::string makeClockDateText(std::time_t timestamp) {
        static constexpr std::array<std::string_view, 7> days{
            "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
        static constexpr std::array<std::string_view, 12> months{
            "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
            "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &timestamp);
#else
        localtime_r(&timestamp, &local);
#endif
        std::ostringstream text;
        text << ' ' << days[static_cast<size_t>(std::clamp(local.tm_wday, 0, 6))]
             << ' ' << local.tm_mday << ' ' << months[static_cast<size_t>(std::clamp(local.tm_mon, 0, 11))]
             << ' ' << (local.tm_year + 1900);
        return text.str();
    }

    // Compact built-in 3x5 font, so the Clock effect never depends on system fonts.
    // It keeps the old-school clock labels/date visible without creating huge glyphs.
    void drawClockGlyph(PHLMONITOR monitor, const Vector2D& pos, char glyph, float height,
                        const Color& color, float alpha) {
        using Bitmap = std::array<std::string_view, 5>;
        Bitmap rows{};
        switch (static_cast<char>(std::toupper(static_cast<unsigned char>(glyph)))) {
            case '0': rows = {"###", "#.#", "#.#", "#.#", "###"}; break;
            case '1': rows = {".#.", "##.", ".#.", ".#.", "###"}; break;
            case '2': rows = {"###", "..#", "###", "#..", "###"}; break;
            case '3': rows = {"###", "..#", ".##", "..#", "###"}; break;
            case '4': rows = {"#.#", "#.#", "###", "..#", "..#"}; break;
            case '5': rows = {"###", "#..", "###", "..#", "###"}; break;
            case '6': rows = {"###", "#..", "###", "#.#", "###"}; break;
            case '7': rows = {"###", "..#", ".#.", ".#.", ".#."}; break;
            case '8': rows = {"###", "#.#", "###", "#.#", "###"}; break;
            case '9': rows = {"###", "#.#", "###", "..#", "###"}; break;
            case 'A': rows = {".#.", "#.#", "###", "#.#", "#.#"}; break;
            case 'B': rows = {"##.", "#.#", "##.", "#.#", "##."}; break;
            case 'C': rows = {".##", "#..", "#..", "#..", ".##"}; break;
            case 'D': rows = {"##.", "#.#", "#.#", "#.#", "##."}; break;
            case 'E': rows = {"###", "#..", "##.", "#..", "###"}; break;
            case 'F': rows = {"###", "#..", "##.", "#..", "#.."}; break;
            case 'G': rows = {".##", "#..", "#.#", "#.#", ".##"}; break;
            case 'H': rows = {"#.#", "#.#", "###", "#.#", "#.#"}; break;
            case 'I': rows = {"###", ".#.", ".#.", ".#.", "###"}; break;
            case 'J': rows = {"..#", "..#", "..#", "#.#", ".#."}; break;
            case 'K': rows = {"#.#", "#.#", "##.", "#.#", "#.#"}; break;
            case 'L': rows = {"#..", "#..", "#..", "#..", "###"}; break;
            case 'M': rows = {"#.#", "###", "###", "#.#", "#.#"}; break;
            case 'N': rows = {"#.#", "###", "###", "###", "#.#"}; break;
            case 'O': rows = {".#.", "#.#", "#.#", "#.#", ".#."}; break;
            case 'P': rows = {"##.", "#.#", "##.", "#..", "#.."}; break;
            case 'Q': rows = {".#.", "#.#", "#.#", ".##", "..#"}; break;
            case 'R': rows = {"##.", "#.#", "##.", "#.#", "#.#"}; break;
            case 'S': rows = {".##", "#..", ".#.", "..#", "##."}; break;
            case 'T': rows = {"###", ".#.", ".#.", ".#.", ".#."}; break;
            case 'U': rows = {"#.#", "#.#", "#.#", "#.#", "###"}; break;
            case 'V': rows = {"#.#", "#.#", "#.#", "#.#", ".#."}; break;
            case 'W': rows = {"#.#", "#.#", "###", "###", "#.#"}; break;
            case 'X': rows = {"#.#", "#.#", ".#.", "#.#", "#.#"}; break;
            case 'Y': rows = {"#.#", "#.#", ".#.", ".#.", ".#."}; break;
            case 'Z': rows = {"###", "..#", ".#.", "#..", "###"}; break;
            case '-': rows = {"...", "...", "###", "...", "..."}; break;
            case '.': rows = {"...", "...", "...", "...", ".#."}; break;
            case ' ': rows = {"...", "...", "...", "...", "..."}; break;
            default: return;
        }
        if (alpha <= 0.01F || height < 3.F)
            return;
        const float cell = height / 5.F;
        const float pixel = std::max(0.65F, cell * 0.82F);
        for (size_t row = 0; row < rows.size(); ++row) {
            for (size_t col = 0; col < rows[row].size(); ++col) {
                if (rows[row][col] != '#')
                    continue;
                const Vector2D offset{(static_cast<double>(col) - 1.0) * cell,
                                      (static_cast<double>(row) - 2.0) * cell};
                addRect(monitor, pos + offset, pixel, pixel, color, alpha, false);
            }
        }
    }

    void drawClockText(PHLMONITOR monitor, const Vector2D& pos, std::string_view text, float height,
                       const Color& color, float alpha) {
        if (text.empty())
            return;
        const float advance = height * 0.76F;
        const float firstOffset = -0.5F * static_cast<float>(text.size() - 1) * advance;
        for (size_t i = 0; i < text.size(); ++i) {
            drawClockGlyph(monitor, pos + Vector2D{firstOffset + static_cast<double>(i) * advance, 0.0},
                           text[i], height, color, alpha);
        }
    }

    bool updateClockPositions(TimePoint now) {
        if (!g_haveLastCursorPos)
            return false;
        const auto dateText = makeClockDateText(std::time(nullptr));
        const size_t nodeCount = dateText.size() + 12 + 3 + 4 + 5;
        if (dateText != g_clockDateText || g_clockNodes.size() != nodeCount) {
            g_clockDateText = dateText;
            g_clockNodes.assign(nodeCount, g_lastCursorPos);
            g_lastClockUpdate = now;
            return true;
        }
        if (g_clockNodes.empty())
            g_clockNodes.assign(nodeCount, g_lastCursorPos);
        const auto oldNodes = g_clockNodes;
        float dt = 1.F / 60.F;
        if (g_lastClockUpdate != TimePoint{})
            dt = std::clamp(std::chrono::duration<float>(now - g_lastClockUpdate).count(), 0.F, 0.05F);
        const float rate = 1.F - std::pow(1.F - THOLMAN_CLOCK_FOLLOW_RATE, dt * 60.F);
        g_clockNodes[0] = lerp(oldNodes[0], g_lastCursorPos, rate);
        for (size_t i = 1; i < g_clockNodes.size(); ++i)
            g_clockNodes[i] = lerp(oldNodes[i], g_clockNodes[i - 1], rate);
        g_lastClockUpdate = now;
        for (size_t i = 0; i < g_clockNodes.size(); ++i)
            if (distance(oldNodes[i], g_clockNodes[i]) > 0.025F)
                return true;
        return false;
    }

    void renderClock(PHLMONITOR monitor, std::time_t timestamp) {
        if (!monitor || g_clockNodes.empty() || g_clockDateText.empty())
            return;
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &timestamp);
#else
        localtime_r(&timestamp, &local);
#endif
        constexpr double PI = 3.14159265358979323846;
        const double secondsAngle = PI * (local.tm_sec - 15) / 30.0;
        const double minutesAngle = PI * (local.tm_min - 15) / 30.0;
        const double hoursAngle = PI * (local.tm_hour % 12 - 3) / 6.0 + PI * local.tm_min / 360.0;
        const double degreesPerDateChar = 360.0 / static_cast<double>(g_clockDateText.size());
        const double radius = g_settings.clockRadius;
        size_t index = 0;

        // The site's nostalgic clock rotates a circular date string around the
        // pointer while the numbered dial and three dotted hands lag behind it.
        for (size_t i = 0; i < g_clockDateText.size(); ++i, ++index) {
            const double angle = -secondsAngle + static_cast<double>(i) * degreesPerDateChar * PI / 180.0;
            const Vector2D pos = g_clockNodes[index] + Vector2D{radius * 1.5 * std::cos(angle), radius * 1.5 * std::sin(angle)};
            if (isNearMonitor(monitor, pos, 24.F))
                drawClockGlyph(monitor, pos, g_clockDateText[i], 8.5F, g_settings.clockDateColor, 1.F);
        }

        static constexpr std::array<std::string_view, 12> dial{"3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "1", "2"};
        for (size_t i = 0; i < dial.size(); ++i, ++index) {
            const double angle = static_cast<double>(i) * 30.0 * PI / 180.0;
            const Vector2D pos = g_clockNodes[index] + Vector2D{radius * std::cos(angle), radius * std::sin(angle)};
            if (isNearMonitor(monitor, pos, 20.F))
                drawClockText(monitor, pos, dial[i], 8.5F, g_settings.clockFaceColor, 1.F);
        }

        const float handStep = static_cast<float>(radius / 6.5);
        auto hand = [&](size_t count, double angle, const Color& color, float size) {
            for (size_t i = 0; i < count; ++i, ++index) {
                const Vector2D pos = g_clockNodes[index] + Vector2D{static_cast<double>(i) * handStep * std::cos(angle),
                                                                    static_cast<double>(i) * handStep * std::sin(angle)};
                if (isNearMonitor(monitor, pos, 12.F))
                    addDot(monitor, pos, size, color, 1.F);
            }
        };
        hand(3, hoursAngle, g_settings.clockHoursColor, 2.5F);
        hand(4, minutesAngle, g_settings.clockMinutesColor, 2.2F);
        hand(5, secondsAngle, g_settings.clockSecondsColor, 1.9F);
    }

    bool updateCursorTails(TimePoint now) {
        if (!g_haveLastCursorPos)
            return false;
        if (g_settings.mode == EffectMode::FollowingDot) {
            const Vector2D old = g_followDotPos;
            if (g_lastTailUpdate == TimePoint{})
                g_followDotPos = g_lastCursorPos;
            else {
                const float dt = std::clamp(std::chrono::duration<float>(now - g_lastTailUpdate).count(), 0.F, 0.05F);
                const float alpha = 1.F - std::pow(0.9F, dt * 60.F);
                g_followDotPos = lerp(g_followDotPos, g_lastCursorPos, alpha);
            }
            g_lastTailUpdate = now;
            return distance(old, g_followDotPos) > 0.025F;
        }

        size_t desired = 0;
        if (g_settings.mode == EffectMode::Trailing)
            desired = THOLMAN_TRAILING_POINTS;
        else if (g_settings.mode == EffectMode::Rainbow)
            desired = THOLMAN_RAINBOW_POINTS;
        if (desired == 0)
            return false;
        if (g_cursorTail.size() != desired)
            g_cursorTail.assign(desired, g_lastCursorPos);

        // Port of rainbowCursor/trailingCursor's linked-particle update:
        // each node is placed at the current interpolated point, then the next
        // point advances 40% toward the next node's PREVIOUS-frame position.
        // The last node wraps toward the current cursor position, as in source.
        const auto oldTail = g_cursorTail;
        float dt = 1.F / 60.F;
        if (g_lastTailUpdate != TimePoint{})
            dt = std::clamp(std::chrono::duration<float>(now - g_lastTailUpdate).count(), 0.F, 0.05F);
        const float rate = 1.F - std::pow(1.F - 0.4F, dt * 60.F);
        Vector2D interpolated = g_lastCursorPos;
        for (size_t i = 0; i < g_cursorTail.size(); ++i) {
            g_cursorTail[i] = interpolated;
            const Vector2D nextOld = (i + 1 < oldTail.size()) ? oldTail[i + 1] : g_lastCursorPos;
            interpolated = lerp(g_cursorTail[i], nextOld, rate);
        }
        g_lastTailUpdate = now;
        for (size_t i = 0; i < g_cursorTail.size(); ++i)
            if (distance(oldTail[i], g_cursorTail[i]) > 0.025F)
                return true;
        return false;
    }

    void addGhostCursor(PHLMONITOR monitor, const Vector2D& cursorPos, float alpha) {
        if (!monitor || !Pointer::mgr() || !g_pHyprRenderer)
            return;
        const auto texture = Pointer::mgr()->getCurrentCursorTexture();
        if (!texture)
            return;
        // Reuse the exact native cursor box computation instead of recreating its
        // size/hotspot arithmetic. This keeps trails aligned to XCursor/Wayland
        // cursor scales and unusual hotspot positions just like Hyprland itself.
        auto cursorBox = Pointer::mgr()->getCursorBoxGlobal();
        cursorBox.translate(cursorPos - Pointer::mgr()->position());
        cursorBox.translate(-monitor->m_position);
        cursorBox.scale(monitor->m_scale);
        if (cursorBox.empty())
            return;
        CTexPassElement::SRenderData data{};
        data.tex = texture;
        data.box = cursorBox.round();
        data.a = std::clamp(alpha, 0.F, 1.F);
        data.overallA = data.a;
        g_pHyprRenderer->addPassElement(Hyprutils::Memory::makeUnique<CTexPassElement>(data));
        ++g_queuedEffectElements;
    }

    void renderParticle(PHLMONITOR monitor, const Particle& particle, float ageSeconds, float life) {
        const float progress = std::clamp(ageSeconds / life, 0.F, 1.F);
        const float remaining = 1.F - progress;
        const Vector2D pos = particlePosition(particle, ageSeconds);

        if (particle.type == EffectMode::FairyDust) {
            // Bright colourful ASCII star glyph; keep a minimum size while fading.
            const float size = std::max(6.5F, particle.size * (0.75F + 0.25F * remaining));
            drawAsciiGlyph(monitor, pos, size, particle.glyph, particle.color,
                           particle.opacity * std::pow(remaining, 0.75F));
        } else if (particle.type == EffectMode::Bubbles) {
            drawBubble(monitor, pos, progress, particle.opacity);
        } else if (particle.type == EffectMode::Snowflakes) {
            // White ASCII snow: *, + and x glyphs fall quickly and fade away.
            const float size = std::max(6.5F, particle.size * (0.70F + 0.30F * remaining));
            drawAsciiGlyph(monitor, pos, size, particle.glyph, {1.F, 1.F, 1.F},
                           particle.opacity * std::pow(remaining, 0.70F));
        }
    }

    void renderEffects(eRenderStage stage) {
        ++g_renderStageEvents;
        if (stage != RENDER_LAST_MOMENT)
            return;
        ++g_lastMomentCallbacks;

        if (!g_pHyprRenderer) {
            ++g_missingRendererFrames;
            return;
        }
        ++g_activeRenderFrames;
        if (!g_settings.enabled || g_settings.mode == EffectMode::Off)
            return;

        auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
        if (!monitor) {
            monitor = State::monitorState()->query().vec(g_lastCursorPos).run();
            if (monitor)
                ++g_cursorMonitorFallbackFrames;
        }
        if (!monitor) {
            ++g_missingMonitorFrames;
            return;
        }

        const auto now = Clock::now();
        const auto oldTail = g_cursorTail;
        const auto oldFollowDot = g_followDotPos;
        const bool tailMoving = updateCursorTails(now);
        const bool clockMoving = g_settings.mode == EffectMode::Clock ? updateClockPositions(now) : false;
        if (g_settings.mode == EffectMode::Clock) {
            // Clock hands tick by the second and the linked characters ease to
            // the pointer. Keep its footprint damaged while active so the second
            // hand/date ring can animate even when the mouse is stationary.
            const float clockDamageRadius = g_settings.clockRadius * 1.5F + 24.F;
            scheduleDamage(monitor, g_lastCursorPos, clockDamageRadius);
            if (clockMoving)
                scheduleDamage(monitor, g_lastCursorPos, clockDamageRadius + 12.F);
        }

        // Only keep damaging the output while an eased trail is still moving.
        // Once it catches up, it stops requesting frames and returns to zero idle cost.
        if (tailMoving) {
            for (const auto& oldPos : oldTail)
                scheduleDamage(monitor, oldPos, 24.F);
            for (const auto& newPos : g_cursorTail)
                scheduleDamage(monitor, newPos, 24.F);
            if (g_settings.mode == EffectMode::FollowingDot) {
                scheduleDamage(monitor, oldFollowDot, 24.F);
                scheduleDamage(monitor, g_followDotPos, 24.F);
            }
        }

        // Before deleting expired particles, damage their last visible area so they do not leave marks.
        for (const auto& particle : g_particles) {
            const float age = std::chrono::duration<float>(now - particle.born).count();
            const float life = std::max(0.01F, particle.lifetimeMs / 1000.F);
            if (age >= life) {
                const auto lastPos = particlePosition(particle, std::max(0.F, life - 1.F / 60.F));
                scheduleDamage(monitor, lastPos, particle.size * 3.F + 12.F);
            }
        }
        std::erase_if(g_particles, [&](const Particle& particle) {
            return now - particle.born >= std::chrono::milliseconds(particle.lifetimeMs);
        });

        for (const auto& particle : g_particles) {
            const float ageSeconds = std::chrono::duration<float>(now - particle.born).count();
            const float life = std::max(0.01F, particle.lifetimeMs / 1000.F);
            const auto pos = particlePosition(particle, ageSeconds);
            const float damageRadius = particle.type == EffectMode::Ghost ? 28.F : particle.size * 2.5F + 12.F;
            if (!isNearMonitor(monitor, pos, damageRadius))
                continue;

            if (particle.type == EffectMode::Ghost) {
                const float alpha = std::clamp(1.F - ageSeconds / life, 0.F, 1.F);
                addGhostCursor(monitor, particle.origin, alpha);
            } else {
                renderParticle(monitor, particle, ageSeconds, life);
            }
            // Mark both the previous-frame and current location to avoid stale ghosting.
            scheduleDamage(monitor, particlePosition(particle, std::max(0.F, ageSeconds - 1.F / 60.F)), damageRadius);
            scheduleDamage(monitor, pos, damageRadius);
        }

        if (g_settings.mode == EffectMode::Trailing) {
            for (const auto& pos : g_cursorTail) {
                if (!isNearMonitor(monitor, pos, 36.F))
                    continue;
                addGhostCursor(monitor, pos, 1.F);
            }
        } else if (g_settings.mode == EffectMode::Rainbow) {
            // Port of rainbowCursor.js: six continuous round-capped colour bands
            // over 20 linked points. The first point is offset by i*(size-1),
            // all subsequent points by i*size. Dots overlap to emulate Canvas
            // lineCap="round" without creating a separate texture.
            const float bandSize = static_cast<float>(g_settings.rainbowSize);
            for (size_t colorIndex = 0; colorIndex < THOLMAN_RAINBOW_COLORS.size(); ++colorIndex) {
                if (g_cursorTail.size() < 2)
                    break;
                const double firstOffset = static_cast<double>(colorIndex) * (bandSize - 1.F);
                const double offset = static_cast<double>(colorIndex) * bandSize;
                Vector2D previous = g_cursorTail.front() + Vector2D{0.0, firstOffset};
                for (size_t i = 1; i < g_cursorTail.size(); ++i) {
                    const Vector2D next = g_cursorTail[i] + Vector2D{0.0, offset};
                    if (isNearMonitor(monitor, previous, 30.F) || isNearMonitor(monitor, next, 30.F))
                        addSegmentDots(monitor, previous, next, bandSize, 1.5F, THOLMAN_RAINBOW_COLORS[colorIndex], 1.F);
                    previous = next;
                }
            }
        } else if (g_settings.mode == EffectMode::FollowingDot) {
            // Source followingDotCursor: a filled 10px-radius dot with a 10-frame lag.
            addDot(monitor, g_followDotPos, 20.F, g_settings.followingDot, g_settings.followingDotAlpha);
        } else if (g_settings.mode == EffectMode::Clock) {
            renderClock(monitor, std::time(nullptr));
        }

    }

    void notify(std::string message, bool success = true) {
        if (!g_handle)
            return;
        HyprlandAPI::addNotification(g_handle, "[HyprCursorFX] " + message,
                                     success ? CHyprColor{0.34F, 0.92F, 0.72F, 1.F} : CHyprColor{1.F, 0.32F, 0.36F, 1.F}, 3000);
    }

    void clearRuntimeState() {
        g_particles.clear();
        g_cursorTail.clear();
        g_haveLastCursorPos = false;
        g_haveBubbleSpawn = false;
        g_haveFairyDustSpawn = false;
        g_haveSnowflakeSpawn = false;
        g_haveGhostSpawn = false;
        g_lastTailUpdate = TimePoint{};
        g_lastClockUpdate = TimePoint{};
        g_clockNodes.clear();
        g_clockDateText.clear();
    }

    SDispatchResult dispatch(std::string args) {
        std::stringstream stream(trim(std::move(args)));
        std::string command;
        stream >> command;
        command = lower(command);
        std::string value;
        std::getline(stream, value);
        value = trim(std::move(value));

        if (command.empty() || command == "status") {
            notify("mode=" + modeName(g_settings.mode) + ", particles=" + std::to_string(g_particles.size()));
            return {.success = true, .error = {}};
        }
        if (command == "reload") {
            Settings next;
            std::string error;
            const bool loaded = readSettings(next, error);
            if (loaded) {
                g_settings = std::move(next);
                g_previousMode = g_settings.mode == EffectMode::Off ? g_previousMode : g_settings.mode;
                clearRuntimeState();
                notify("config reloaded; mode=" + modeName(g_settings.mode));
                return {.success = true, .error = {}};
            }
            notify(error, false);
            return {.success = false, .error = error};
        }
        if (command == "mode") {
            bool valid = false;
            const auto mode = parseMode(value, &valid);
            if (!valid) {
                const std::string error = "unknown mode. Use off, fairy_dust, ghost, trailing, bubbles, snowflakes, rainbow, following_dot or clock.";
                notify(error, false);
                return {.success = false, .error = error};
            }
            if (mode != EffectMode::Off)
                g_previousMode = mode;
            g_settings.mode = mode;
            g_settings.enabled = true;
            clearRuntimeState();
            if (mode != EffectMode::Off && Pointer::mgr())
                seedEffectAt(Pointer::mgr()->position());
            notify("mode=" + modeName(mode));
            return {.success = true, .error = {}};
        }
        if (command == "toggle") {
            if (g_settings.mode == EffectMode::Off) {
                g_settings.enabled = true;
                g_settings.mode = g_previousMode == EffectMode::Off ? EffectMode::FairyDust : g_previousMode;
            } else {
                g_previousMode = g_settings.mode;
                g_settings.mode = EffectMode::Off;
            }
            clearRuntimeState();
            if (g_settings.mode != EffectMode::Off && Pointer::mgr())
                seedEffectAt(Pointer::mgr()->position());
            notify("mode=" + modeName(g_settings.mode));
            return {.success = true, .error = {}};
        }
        if (command == "off" || command == "stop") {
            g_previousMode = g_settings.mode == EffectMode::Off ? g_previousMode : g_settings.mode;
            g_settings.mode = EffectMode::Off;
            clearRuntimeState();
            notify("effects disabled");
            return {.success = true, .error = {}};
        }

        // Also accept a bare mode name from legacy dispatcher-based configurations.
        bool valid = false;
        const auto mode = parseMode(command + (value.empty() ? "" : " " + value), &valid);
        if (valid) {
            if (mode != EffectMode::Off)
                g_previousMode = mode;
            g_settings.mode = mode;
            g_settings.enabled = true;
            clearRuntimeState();
            if (mode != EffectMode::Off && Pointer::mgr())
                seedEffectAt(Pointer::mgr()->position());
            notify("mode=" + modeName(mode));
            return {.success = true, .error = {}};
        }
        const std::string error = "usage: hyprcursorfx mode <off|fairy_dust|ghost|trailing|bubbles|snowflakes|rainbow|following_dot|clock>, reload, toggle, status";
        notify(error, false);
        return {.success = false, .error = error};
    }

    std::string statusText() {
        std::ostringstream out;
        out << "HyprCursorFX " << VERSION
            << "\nmode=" << modeName(g_settings.mode)
            << " enabled=" << (g_settings.enabled ? "true" : "false")
            << " particles=" << g_particles.size()
            << " tail_points=" << g_cursorTail.size()
            << " clock_nodes=" << g_clockNodes.size()
            << " mouse_move_events=" << g_mouseMoveEvents
            << " render_callbacks=" << g_renderStageEvents
            << " last_moment_callbacks=" << g_lastMomentCallbacks
            << " renderer_available=" << (g_pHyprRenderer ? "true" : "false")
            << " active_render_frames=" << g_activeRenderFrames
            << " missing_renderer_frames=" << g_missingRendererFrames
            << " missing_monitor_frames=" << g_missingMonitorFrames
            << " cursor_monitor_fallback_frames=" << g_cursorMonitorFallbackFrames
            << " queued_effect_elements=" << g_queuedEffectElements;
        return out.str();
    }

    std::string handleHyprCtlRequest(std::string request) {
        request = trim(std::move(request));
        std::stringstream input(request);
        std::string first;
        input >> first;
        if (lower(first) == "hyprcursorfx") {
            std::getline(input, request);
            request = trim(std::move(request));
        } else {
            request = trim(std::move(request));
        }

        if (request.empty() || lower(request) == "status")
            return statusText();

        const auto result = dispatch(request);
        if (!result.success)
            return result.error.empty() ? "HyprCursorFX command failed" : result.error;
        return "ok";
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_handle = handle;
    g_mouseMoveEvents = 0;
    g_renderStageEvents = 0;
    g_lastMomentCallbacks = 0;
    g_activeRenderFrames = 0;
    g_missingRendererFrames = 0;
    g_missingMonitorFrames = 0;
    g_cursorMonitorFallbackFrames = 0;
    g_queuedEffectElements = 0;

    const std::string compositorHash = __hyprland_api_get_hash();
    const std::string pluginHash = __hyprland_api_get_client_hash();
    if (compositorHash != pluginHash) {
        HyprlandAPI::addNotification(g_handle, "[HyprCursorFX] Hyprland header mismatch; refusing to initialize.",
                                     CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 6000);
        throw std::runtime_error("[HyprCursorFX] Hyprland header mismatch");
    }

    Settings initial;
    std::string configError;
    if (readSettings(initial, configError))
        g_settings = std::move(initial);
    else
        g_settings = Settings{};
    g_previousMode = g_settings.mode == EffectMode::Off ? EffectMode::FairyDust : g_settings.mode;

    g_mouseListener.emplace(Event::bus()->m_events.input.mouse.move.listen(onMouseMove));
    g_renderListener.emplace(Event::bus()->m_events.render.stage.listen(renderEffects));

    // Register a native IPC command. This is intentionally independent of the
    // user's config provider: classic hyprland.conf and hyprland.lua can both
    // control the same loaded plugin via `hyprctl hyprcursorfx ...`.
    const auto ctlCommand = HyprlandAPI::registerHyprCtlCommand(g_handle, SHyprCtlCommand{
        "hyprcursorfx", false,
        [](auto /*format*/, std::string request) -> std::string {
            return handleHyprCtlRequest(std::move(request));
        }
    });
    if (!ctlCommand) {
        g_mouseListener.reset();
        g_renderListener.reset();
        throw std::runtime_error("[HyprCursorFX] Failed to register hyprctl command");
    }

    // Retain dispatcher support for Hyprland configurations that still use it.
    HyprlandAPI::addDispatcherV2(g_handle, "hyprcursorfx", dispatch);

    if (g_settings.enabled && g_settings.mode != EffectMode::Off && Pointer::mgr())
        seedEffectAt(Pointer::mgr()->position());

    HyprlandAPI::addNotification(g_handle,
        "[HyprCursorFX v" + std::string{VERSION} + "] loaded; IPC=hyprcursorfx; mode=" + modeName(g_settings.mode),
        CHyprColor{0.34F, 0.92F, 0.72F, 1.F}, 3500);
    return {"HyprCursorFX", "Tholman-style global cursor effects for Hyprland", "HyprCursorFX contributors", std::string{VERSION}};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_mouseListener.reset();
    g_renderListener.reset();
    if (g_handle)
        HyprlandAPI::removeDispatcher(g_handle, "hyprcursorfx");
    clearRuntimeState();
    g_handle = nullptr;
}
