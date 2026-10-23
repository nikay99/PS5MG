// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh_view.hpp"

#include <GL/glcorearb.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "glb_read.hpp"
#include "image_file.hpp"

namespace ps5mg {
namespace {

constexpr size_t kMaxGlb = 64u << 20;  // a GLB from before 0.2.3 without texture can be 130 MB

const char * kVert = R"(#version 330 core
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 a;
uniform mat4 mv;
uniform mat4 proj;
out vec3 vp;
out vec3 va;
void main() { vec4 v = mv * vec4(p, 1.0); vp = v.xyz; va = a; gl_Position = proj * v; })";

// The normal comes from the screen-space derivatives: flat shading, and nothing to compute on the CPU.
const char * kFrag = R"(#version 330 core
in vec3 vp;
in vec3 va;
uniform int mode;
uniform sampler2D tex;
out vec4 color;
void main() {
    vec3 n = normalize(cross(dFdx(vp), dFdy(vp)));
    float l = 0.35 + 0.65 * abs(n.z);
    vec3 base = mode == 2 ? texture(tex, va.xy).rgb : (mode == 1 ? va : vec3(0.72));
    color = vec4(base * l, 1.0);
})";

GLuint compile(GLenum stage, const char * src) {
    GLuint s = glCreateShader(stage);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

// Column-major 4x4, as GL takes it.
struct M { float m[16]; };
M ident() { M r = {}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
M mul(const M & a, const M & b) {
    M r = {};
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++)
            for (int rr = 0; rr < 4; rr++) r.m[c * 4 + rr] += a.m[k * 4 + rr] * b.m[c * 4 + k];
    return r;
}
M rot_y(float a) { M r = ident(); r.m[0] = std::cos(a); r.m[8] = std::sin(a); r.m[2] = -std::sin(a); r.m[10] = std::cos(a); return r; }
M rot_x(float a) { M r = ident(); r.m[5] = std::cos(a); r.m[9] = -std::sin(a); r.m[6] = std::sin(a); r.m[10] = std::cos(a); return r; }
M move(float x, float y, float z) { M r = ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
M scale(float s) { M r = ident(); r.m[0] = r.m[5] = r.m[10] = s; return r; }
M perspective(float fov, float aspect, float n, float f) {
    M r = {};
    const float t = 1.f / std::tan(fov / 2);
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
    return r;
}

}  // namespace

bool MeshView::load(const std::string & glb, const std::string & base_png, std::string & err) {
    unload();
    GlbMesh m;
    if (!read_glb(glb, kMaxGlb, m, err)) return false;

    const GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) { err = "shader"; return false; }
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(prog_); prog_ = 0; err = "shader"; return false; }

    // Centre on the bounding box, the longest side becomes 1.
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < m.pos.size(); i++) {
        mn[i % 3] = std::min(mn[i % 3], m.pos[i]);
        mx[i % 3] = std::max(mx[i % 3], m.pos[i]);
    }
    float side = std::max({mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 1e-6f});
    for (int c = 0; c < 3; c++) center_[c] = (mn[c] + mx[c]) / 2;
    scale_ = 1.f / side;

    mode_ = m.attr_n == 3 ? 1 : 0;
    if (m.attr_n == 2) {
        ps5sd::Rgba img;
        if (ps5sd::load_image_rgba(base_png, 1024, img) && img.width > 0) {
            glGenTextures(1, &tex_);
            glBindTexture(GL_TEXTURE_2D, tex_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            mode_ = 2;
        }
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(3, vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_[0]);
    glBufferData(GL_ARRAY_BUFFER, m.pos.size() * 4, m.pos.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    if (m.attr_n > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_[1]);
        glBufferData(GL_ARRAY_BUFFER, m.attr.size() * 4, m.attr.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(1, m.attr_n, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(1);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo_[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.idx.size() * 4, m.idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    index_count_ = static_cast<int>(m.idx.size());
    yaw_ = 0.6f; pitch_ = 0.25f; dist_ = 2.0f;
    return true;
}

void MeshView::unload() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_[0] || vbo_[1] || vbo_[2]) { glDeleteBuffers(3, vbo_); vbo_[0] = vbo_[1] = vbo_[2] = 0; }
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
}

void MeshView::turn(float dyaw, float dpitch) {
    yaw_ += dyaw;
    pitch_ = std::clamp(pitch_ + dpitch, -1.5f, 1.5f);
}

void MeshView::zoom(float dz) { dist_ = std::clamp(dist_ + dz, 1.1f, 6.f); }

void MeshView::draw(int w, int h) {
    if (!vao_) return;
    const M model = mul(scale(scale_), move(-center_[0], -center_[1], -center_[2]));
    const M mv    = mul(move(0, 0, -dist_), mul(rot_x(pitch_), mul(rot_y(yaw_), model)));
    const M proj  = perspective(0.7f, static_cast<float>(w) / h, 0.1f, 20.f);

    glViewport(0, 0, w, h);
    glClearColor(0.07f, 0.08f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "mv"), 1, GL_FALSE, mv.m);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "proj"), 1, GL_FALSE, proj.m);
    glUniform1i(glGetUniformLocation(prog_, "mode"), mode_);
    glUniform1i(glGetUniformLocation(prog_, "tex"), 0);
    if (mode_ == 2) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex_); }
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
}

}  // namespace ps5mg
