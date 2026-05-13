#include "renderer/gl/shaders.h"

namespace renderer::gl {

const char* kSolidVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_position;
uniform mat4 u_projection;
void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
}
)";

const char* kSolidFragmentShader = R"(
#version 330 core
uniform vec4 u_color;
out vec4 fragColor;
void main() {
    fragColor = u_color;
}
)";

const char* kHatchFragmentShader = R"(
#version 330 core
const int yScale = 4;
const int spacing = 50;
const int width = 5;
uniform float u_offset;
uniform vec4 u_color;
out vec4 fragColor;
void main() {
    if (mod(u_offset + gl_FragCoord.x - (yScale * gl_FragCoord.y), spacing) > width) {
        discard;
    }
    fragColor = u_color;
}
)";

const char* kColoredVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec3 a_color;
uniform mat4 u_projection;
out vec3 v_color;
void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_color = a_color;
}
)";

const char* kColoredFragmentShader = R"(
#version 330 core
in vec3 v_color;
out vec4 fragColor;
void main() {
    fragColor = vec4(v_color, 1.0);
}
)";

const char* kTexturedVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
uniform mat4 u_projection;
out vec2 v_uv;
void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_uv = a_uv;
}
)";

const char* kTexturedFragmentShader = R"(
#version 330 core
uniform sampler2D u_texture;
uniform vec4 u_color;
in vec2 v_uv;
out vec4 fragColor;
void main() {
    fragColor = texture(u_texture, v_uv) * u_color;
}
)";

const char* kFontVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_backgroundColor;
uniform mat4 u_projection;
out vec2 v_uv;
out vec4 v_color;
out vec4 v_backgroundColor;
void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
    v_backgroundColor = a_backgroundColor;
}
)";

const char* kFontFragmentShader = R"(
#version 330 core
uniform sampler2D u_fontAtlas;
in vec2 v_uv;
in vec4 v_color;
in vec4 v_backgroundColor;
out vec4 fragColor;
void main() {
    float textAlpha = texture(u_fontAtlas, v_uv).r;
    fragColor = mix(v_backgroundColor, vec4(v_color.rgb, textAlpha), textAlpha);
}
)";

} // namespace renderer::gl
