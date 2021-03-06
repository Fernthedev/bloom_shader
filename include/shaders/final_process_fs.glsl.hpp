
constexpr const char* final_process_fs_glsl = "#version 310 es\n"
"\n"
"precision mediump float;\n"
"\n"
"out vec4 FragColor;\n"
"\n"
"in vec2 TexCoords;\n"
"\n"
"uniform sampler2D scene;\n"
"uniform sampler2D bloomBlur;\n"
"uniform float exposure;\n"
"\n"
"void main()\n"
"{\n"
"    const float gamma = 2.2;\n"
"    vec3 hdrColor = texture(scene, TexCoords).rgb;\n"
"    vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;\n"
"    hdrColor += bloomColor; // additive blending\n"
"    // tone mapping\n"
"    vec3 result = vec3(1.0) - exp(-hdrColor * exposure);\n"
"    // also gamma correct while we're at it\n"
"    result = pow(result, vec3(1.0 / gamma));\n"
"    FragColor = vec4(result, 1.0);\n"
"}\n"
;
