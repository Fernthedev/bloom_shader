
constexpr const char* gaussian_vs_glsl = "#version 310 es\n"
"\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aTexCoords;\n"
"\n"
"out vec2 TexCoords;\n"
"\n"
"void main()\n"
"{\n"
"    TexCoords = aTexCoords;\n"
"    gl_Position = vec4(aPos, 1.0);\n"
"}\n"
;
