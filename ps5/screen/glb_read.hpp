// Reading the GLBs this title writes (trellis.cpp's write_glb and write_glb_textured), nothing else.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps5mg {

struct GlbMesh {
    std::vector<float>    pos;    // xyz per vertex
    std::vector<float>    attr;   // rgb per vertex (attr_n 3) or uv per vertex (attr_n 2); empty when attr_n is 0
    int                   attr_n = 0;
    std::vector<uint32_t> idx;    // three per face, every one below the vertex count
};

// The layout is fixed: untextured = POSITION, indices, [COLOR_0]; textured = POSITION, NORMAL, TEXCOORD_0, indices.
// A file of another layout, a damaged one, or one over max_bytes is refused with a reason in err.
bool parse_glb(const uint8_t * data, size_t size, GlbMesh & out, std::string & err);
bool read_glb(const std::string & path, size_t max_bytes, GlbMesh & out, std::string & err);

}  // namespace ps5mg
