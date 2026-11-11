// Ported from Shadertoy ES (#version 300 es → GL 2.x / gl_FragColor).
#ifdef GL_ES
precision mediump float;
#endif

uniform float uTime;
uniform vec2 uResolution;
varying vec2 v_uv;

void main() {
    vec2 fragCoord = v_uv * uResolution;
    vec2 uv = (2.0 * fragCoord - uResolution.xy) / min(uResolution.x, uResolution.y);

    for (float i = 1.0; i < 6.0; i++) {
        uv.x += 0.6 / i * cos(i * 2.5 * uv.y + (uTime / 18.0));
        uv.y += 0.6 / i * cos(i * 2.5 * uv.x + (uTime / 18.0));
    }

    float wave = 0.5 + 0.5 * sin(uTime - uv.y - uv.x);
    vec3 basePurple = vec3(0.439, 0.306, 1.0);
    vec3 lightWave = vec3(0.514, 0.376, 1.0);
    vec3 finalColor = mix(basePurple, lightWave, wave);
    gl_FragColor = vec4(finalColor, 1.0);
}
