// Reference vertex for the desktop sidebar wave (NDC fullscreen strip).
// Live path: embedded twin in desktop_menu_sidebar_wave_gpu.cpp.
attribute vec2 a_position;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
}
