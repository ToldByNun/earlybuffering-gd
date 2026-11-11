// Ported from Shadertoy ES (#version 300 es → GL 2.x / gl_FragColor).
// Live desktop menu paints the CPU twin in desktop_menu_sidebar_smoke.cpp,
// driven by desktop-accent-color (uBaseColor / uLightColor equivalents).
#ifdef GL_ES
precision mediump float;
#endif

uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uBaseColor;
uniform vec3 uLightColor;
varying vec2 v_uv;

void main() {
    vec2 fragCoord = v_uv * uResolution;
    vec2 uv = (2.0 * fragCoord - uResolution.xy) / min(uResolution.x, uResolution.y);

    for (float i = 1.0; i < 6.0; i++) {
        uv.x += 0.6 / i * cos(i * 2.5 * uv.y + (uTime / 18.0));
        uv.y += 0.6 / i * cos(i * 2.5 * uv.x + (uTime / 18.0));
    }

    float wave = 0.5 + 0.5 * sin(uTime - uv.y - uv.x);
    vec3 finalColor = mix(uBaseColor, uLightColor, wave);
    gl_FragColor = vec4(finalColor, 1.0);
}
