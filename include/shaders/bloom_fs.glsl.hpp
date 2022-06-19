
constexpr const char* bloom_fs_glsl = "#version 310 es\n"
"\n"
"precision mediump float;\n"
"\n"
"uniform sampler2D cameraTexture;\n"
"\n"
"layout (location = 0) in vec2 texCoords;\n"
"layout (location = 1) out vec4 BrightColor;\n"
"\n"
"\n"
"\n"
"void main()\n"
"{\n"
"    // check whether fragment output is higher than threshold, if so output as brightness color\n"
"    vec3 color = texture(cameraTexture, texCoords).rgb;\n"
"    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
"    if(brightness > 1.0)\n"
"    BrightColor = vec4(color, 1.0);\n"
"    else\n"
"    BrightColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
"}\n"
;
