// SPDX-License-Identifier: GPL-3.0-or-later
#include "glb_read.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ps5mg {
namespace {

uint32_t u32(const uint8_t * p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

struct View {
    uint32_t off = 0, len = 0;
};

// The bufferViews array in order: every entry of our files is {"buffer":0,"byteOffset":N,"byteLength":N...}.
std::vector<View> views_of(const std::string & json) {
    std::vector<View> v;
    size_t at = json.find("\"bufferViews\":[");
    if (at == std::string::npos) return v;
    const size_t end = json.find(']', at);
    for (size_t p = json.find("\"byteOffset\":", at); p != std::string::npos && p < end; p = json.find("\"byteOffset\":", p + 1)) {
        const size_t l = json.find("\"byteLength\":", p);
        if (l == std::string::npos || l >= end) break;
        View w;
        w.off = static_cast<uint32_t>(std::strtoul(json.c_str() + p + 13, nullptr, 10));
        w.len = static_cast<uint32_t>(std::strtoul(json.c_str() + l + 13, nullptr, 10));
        v.push_back(w);
    }
    return v;
}

}  // namespace

bool parse_glb(const uint8_t * d, size_t size, GlbMesh & out, std::string & err) {
    out = GlbMesh();
    if (size < 28 || u32(d) != 0x46546C67 || u32(d + 4) != 2) { err = "not a GLB"; return false; }
    const uint32_t jlen = u32(d + 12);
    if (u32(d + 16) != 0x4E4F534A || 20 + (size_t) jlen + 8 > size) { err = "damaged GLB"; return false; }
    const std::string json(reinterpret_cast<const char *>(d + 20), jlen);
    const size_t      bin_at = 20 + (size_t) jlen + 8;
    const uint32_t    blen   = u32(d + 20 + jlen);
    if (u32(d + 24 + jlen) != 0x004E4942 || bin_at + blen > size) { err = "damaged GLB"; return false; }
    const uint8_t * bin = d + bin_at;

    const bool textured = json.find("\"TEXCOORD_0\"") != std::string::npos;
    const auto views    = views_of(json);
    const size_t pos_i = 0, idx_i = textured ? 3 : 1, attr_i = 2;
    if (views.size() < idx_i + 1) { err = "unexpected layout"; return false; }
    for (const View & w : views) {
        if ((size_t) w.off + w.len > blen) { err = "damaged GLB"; return false; }
    }
    const size_t V = views[pos_i].len / 12, I = views[idx_i].len / 4;
    if (V == 0 || I < 3) { err = "empty mesh"; return false; }

    out.pos.resize(V * 3);
    std::memcpy(out.pos.data(), bin + views[pos_i].off, V * 12);
    out.idx.resize(I / 3 * 3);
    std::memcpy(out.idx.data(), bin + views[idx_i].off, out.idx.size() * 4);
    for (uint32_t i : out.idx) {
        if (i >= V) { err = "index out of range"; return false; }
    }
    if (textured && views[attr_i].len >= V * 8) {
        out.attr_n = 2;
        out.attr.resize(V * 2);
        std::memcpy(out.attr.data(), bin + views[attr_i].off, V * 8);
    } else if (!textured && views.size() > attr_i && views[attr_i].len >= V * 12) {
        out.attr_n = 3;
        out.attr.resize(V * 3);
        std::memcpy(out.attr.data(), bin + views[attr_i].off, V * 12);
    }
    return true;
}

bool read_glb(const std::string & path, size_t max_bytes, GlbMesh & out, std::string & err) {
    FILE * f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "no such file"; return false; }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || (size_t) n > max_bytes) {
        std::fclose(f);
        err = "file too large";
        return false;
    }
    std::vector<uint8_t> buf((size_t) n);
    const bool ok = std::fread(buf.data(), 1, buf.size(), f) == buf.size();
    std::fclose(f);
    if (!ok) { err = "read failed"; return false; }
    return parse_glb(buf.data(), buf.size(), out, err);
}

}  // namespace ps5mg
