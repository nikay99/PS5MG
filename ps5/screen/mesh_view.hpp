// A finished model on the screen: flat-shaded, turned with the stick. Draws straight into the current framebuffer.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace ps5mg {

class MeshView {
public:
    ~MeshView() { unload(); }
    // glb and (optionally) the colour picture beside it; false with a reason in err.
    bool load(const std::string & glb, const std::string & base_png, std::string & err);
    void unload();  // gives the GL buffers back: they come from the pool the models use
    bool loaded() const { return vao_ != 0; }
    void turn(float dyaw, float dpitch);
    void zoom(float dz);
    void draw(int w, int h);

private:
    unsigned vao_ = 0, vbo_[3] = {}, tex_ = 0, prog_ = 0;
    int      index_count_ = 0, mode_ = 0;
    float    center_[3] = {}, scale_ = 1, yaw_ = 0.6f, pitch_ = 0.25f, dist_ = 2.0f;
};

}  // namespace ps5mg
