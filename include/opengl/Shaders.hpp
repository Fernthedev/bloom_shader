#pragma once

#include "Shader.hpp"

#include "shaders/bloom_fs.glsl.hpp"
#include "shaders/bloom_vs.glsl.hpp"

#include "shaders/final_process_fs.glsl.hpp"
#include "shaders/final_process_vs.glsl.hpp"

#include "shaders/gaussian_fs.glsl.hpp"
#include "shaders/gaussian_vs.glsl.hpp"

#define shader_macro(s) \
static Shader s() { \
    return {s##_vs_glsl, s##_fs_glsl}; \
}

namespace Shaders {
    shader_macro(gaussian)

    shader_macro(final_process)

    shader_macro(bloom)
}