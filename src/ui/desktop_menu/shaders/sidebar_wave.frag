// Reference fragment for the desktop sidebar wave.
// Live path: embedded twin in desktop_menu_sidebar_wave_gpu.cpp (ImDrawCallback,
// no custom FBO). CPU fallback: desktop_menu_sidebar_smoke.cpp.
// Driven by desktop-accent-color (uBaseColor / uLightColor).
#ifdef GL_ES
precision mediump float;
#endif

uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uBaseColor;
uniform vec3 uLightColor;
uniform float uRounding;
uniform vec4 uCorners; // tl, tr, br, bl
uniform vec2 uRectMin;
uniform float uFbHeight;

void main() {
    vec2 topLeft = vec2(gl_FragCoord.x, uFbHeight - gl_FragCoord.y);
    vec2 local = topLeft - uRectMin;
    if (local.x < 0.0 || local.y < 0.0 || local.x > uResolution.x || local.y > uResolution.y) {
        discard;
    }

    float r = uRounding;
    if (r > 0.5) {
        vec2 tl = local - vec2(r, r);
        vec2 tr = local - vec2(uResolution.x - r, r);
        vec2 br = local - vec2(uResolution.x - r, uResolution.y - r);
        vec2 bl = local - vec2(r, uResolution.y - r);
        if (uCorners.x > 0.5 && local.x < r && local.y < r && dot(tl, tl) > r * r) discard;
        if (uCorners.y > 0.5 && local.x > uResolution.x - r && local.y < r &&
            dot(tr, tr) > r * r)
            discard;
        if (uCorners.z > 0.5 && local.x > uResolution.x - r && local.y > uResolution.y - r &&
            dot(br, br) > r * r)
            discard;
        if (uCorners.w > 0.5 && local.x < r && local.y > uResolution.y - r &&
            dot(bl, bl) > r * r)
            discard;
    }

    vec2 fragCoord = local;
    vec2 uv = (2.0 * fragCoord - uResolution.xy) / min(uResolution.x, uResolution.y);
    for (float i = 1.0; i < 6.0; i++) {
        uv.x += 0.6 / i * cos(i * 2.5 * uv.y + (uTime / 18.0));
        uv.y += 0.6 / i * cos(i * 2.5 * uv.x + (uTime / 18.0));
    }
    float wave = 0.5 + 0.5 * sin(uTime - uv.y - uv.x);
    vec3 finalColor = mix(uBaseColor, uLightColor, wave);
    gl_FragColor = vec4(finalColor, 1.0);
}
