#version 300 es
precision mediump float;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

uniform sampler2D tex;
uniform float time;

// ------------------------------------------------------------
// Christmas snowfall for Hyprland screen_shader
// No external textures or assets required.
// ------------------------------------------------------------

float hash1(float n) {
    return fract(sin(n * 127.1) * 43758.5453123);
}

float hash2(float n) {
    return fract(sin(n * 311.7 + 17.3) * 26942.1534);
}

vec2 flakePosition(float columnId, float columns, float t, float speed, float drift) {
    float cell = 1.0 / columns;

    float phase = hash1(columnId + 10.0);
    float xJitter = (hash2(columnId + 40.0) - 0.5) * cell * 0.72;
    float y = fract(phase + t * speed);

    // Gentle side-to-side movement. Each flake gets its own phase.
    float swayPhase = hash1(columnId + 90.0) * 6.2831853;
    float sway = sin(t * 0.55 + swayPhase + y * 3.0) * cell * drift;

    float x = (columnId + 0.5) * cell + xJitter + sway;
    return vec2(x, y);
}

float drawLayer(vec2 uv, float columns, float t, float speed, float size, float drift) {
    float cell = 1.0 / columns;
    float columnId = floor(uv.x * columns);
    float centerX = (columnId + 0.5) * cell;

    // Current column plus a neighbour on either side avoids visible seams.
    float glow = 0.0;
    for (int offset = -1; offset <= 1; ++offset) {
        float id = columnId + float(offset);
        vec2 p = flakePosition(id, columns, t, speed, drift);

        vec2 d = uv - p;
        d.x = abs(d.x);

        float radius = cell * size;
        float core = 1.0 - smoothstep(radius * 0.28, radius, length(d));
        float halo = 1.0 - smoothstep(radius * 0.9, radius * 1.75, length(d));

        // Tiny twinkle gives the snow a more festive feel.
        float twinkle = 0.82 + 0.18 * sin(t * 1.8 + hash1(id + 200.0) * 6.2831853);
        glow = max(glow, (core * 0.95 + halo * 0.35) * twinkle);
    }

    return glow;
}

void main() {
    vec4 base = texture(tex, v_texcoord);
    vec2 uv = v_texcoord;

    // Three depth layers: large/slow foreground, medium, fine fast snow.
    float foreground = drawLayer(uv, 34.0, time, 0.11, 0.82, 0.75);
    float midground  = drawLayer(uv, 58.0, time, 0.17, 0.60, 0.95);
    float background = drawLayer(uv, 92.0, time, 0.23, 0.42, 1.20);

    float snow = foreground * 0.95 + midground * 0.62 + background * 0.38;

    // A very subtle blue-white winter glow; it only appears where snow is present.
    vec3 snowColor = mix(vec3(1.0), vec3(0.86, 0.94, 1.0), 0.18);
    float alpha = clamp(snow, 0.0, 0.96);

    fragColor = vec4(mix(base.rgb, snowColor, alpha), base.a);
}
