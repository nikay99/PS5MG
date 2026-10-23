// Host check of the GLB reader: glb_read_test <file.glb> [textured|colored|plain]. Prints the counts, asserts they are sane.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>

#include "glb_read.hpp"

int main(int argc, char ** argv) {
    if (argc < 3) return 2;
    ps5mg::GlbMesh m;
    std::string    err;
    if (!ps5mg::read_glb(argv[1], 256u << 20, m, err)) {
        std::printf("FAIL %s: %s\n", argv[1], err.c_str());
        return 1;
    }
    const size_t V = m.pos.size() / 3, F = m.idx.size() / 3;
    std::printf("%s: V=%zu F=%zu attr_n=%d\n", argv[1], V, F, m.attr_n);
    assert(V > 0 && F > 0);
    const int want = !std::strcmp(argv[2], "textured") ? 2 : !std::strcmp(argv[2], "colored") ? 3 : 0;
    assert(m.attr_n == want);
    assert(m.attr.size() == V * want);
    std::string e2;
    ps5mg::GlbMesh bad;
    const unsigned char junk[40] = {0};
    assert(!ps5mg::parse_glb(junk, sizeof(junk), bad, e2));  // refuses what is not a GLB
    std::puts("ok");
    return 0;
}
