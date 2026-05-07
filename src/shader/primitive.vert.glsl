#version 460

// Solid-shaded primitive shader (added by SPBF64 fork).
// Per-vertex lighting is baked into the color on the CPU side, so this
// shader is essentially a pass-through plus picking ID writeout.
layout (location = 0) in vec3 inPosition;
layout (location = 1) in uint inObjectId;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec4 v_color;
layout (location = 1) out flat uint v_objectID;

layout(std140, set = 1, binding = 0) uniform UniformGlobal {
    mat4 projMat;
    mat4 cameraMat;
    vec2 screenSize;
};

layout(std140, set = 1, binding = 1) uniform UniformObject {
    mat4 modelMat;
    uint objectID;
};

void main()
{
  mat4 matMVP = projMat * cameraMat * modelMat;
  gl_Position = matMVP * vec4(inPosition, 1.0);
  v_color = inColor;
  v_objectID = inObjectId;
}
