#version 450
// Optional postprocessing variant of the LCD transfer: reads a render target (rows in memory order, as the PICA
// lays them out) and writes the screen as it appears, upright. The LCDs scan out rotated, so
// a screen column is a row of the transfer's output. Downscaling averages 2x1 or 2x2 blocks
// by sampling bilinearly at the block's center.

layout(set = 2, binding = 0) uniform sampler2D src;

layout(set = 3, binding = 0) uniform Blit {
    vec4 size;    // x: output columns (screen width), y: output rows (screen height), z/w: source size
    ivec4 mode;   // x: flip, y/z: downscale, w: optional top-screen FXAA bit + sharpness percentage << 1
};

layout(location = 0) out vec4 color;
float luma(vec3 rgb) { return dot(rgb, vec3(0.299, 0.587, 0.114)); }
vec3 sample_rgb(vec2 uv) { return textureLod(src, uv, 0.0).rgb; }

void main() {
    int sx = int(gl_FragCoord.x), sy = int(gl_FragCoord.y);
    int rows = int(size.x), column = int(size.y);
    // transfer output pixel (x, y): x along the screen's height, y along its width
    int x = column - 1 - sy;
    int y = mode.x != 0 ? rows - 1 - sx : sx;
    vec2 block = vec2(1 << mode.y, 1 << mode.z);
    vec2 texel = vec2(x, y) * block + block * 0.5;
    vec2 uv = texel / size.zw;
    vec3 center = sample_rgb(uv);
    // Exactly the original single sample when both effects are disabled.
    if (mode.w == 0) {
        color = vec4(center, 1.0);
        return;
    }
    vec2 step_uv = block / size.zw;
    if ((mode.w & 1) != 0) {
        // Single-pass directional luma edge smoothing (FXAA), at LCD resolution.
        float nw = luma(sample_rgb(uv + step_uv * vec2(-1,-1)));
        float ne = luma(sample_rgb(uv + step_uv * vec2( 1,-1)));
        float sw = luma(sample_rgb(uv + step_uv * vec2(-1, 1)));
        float se = luma(sample_rgb(uv + step_uv * vec2( 1, 1)));
        float m = luma(center);
        float low = min(m, min(min(nw,ne), min(sw,se)));
        float high = max(m, max(max(nw,ne), max(sw,se)));
        if (high - low > max(0.04, high * 0.12)) {
            vec2 tangent = vec2(sw + se - nw - ne, nw + sw - ne - se);
            float reduce = max((nw + ne + sw + se) * 0.03125, 0.0078125);
            tangent = clamp(tangent / (min(abs(tangent.x),abs(tangent.y)) + reduce), vec2(-6), vec2(6)) * step_uv;
            vec3 inner = (sample_rgb(uv - tangent / 6.0) + sample_rgb(uv + tangent / 6.0)) * 0.5;
            vec3 outer = inner * 0.5 + (sample_rgb(uv - tangent * 0.5) + sample_rgb(uv + tangent * 0.5)) * 0.25;
            float outer_luma = luma(outer);
            center = outer_luma < low || outer_luma > high ? inner : outer;
        }
    }
    if ((mode.w >> 1) != 0) {
        vec3 north = sample_rgb(uv - vec2(0,step_uv.y));
        vec3 south = sample_rgb(uv + vec2(0,step_uv.y));
        vec3 east = sample_rgb(uv + vec2(step_uv.x,0));
        vec3 west = sample_rgb(uv - vec2(step_uv.x,0));
        vec3 lo = min(center, min(min(north,south), min(east,west)));
        vec3 hi = max(center, max(max(north,south), max(east,west)));
        float strength = float(mode.w >> 1) * 0.006;
        center = clamp(center + strength * (center - (north+south+east+west) * 0.25), lo, hi);
    }
    color = vec4(center, 1.0);
}
