// PS5MG's QEM edge-collapse simplifier, in place of trellis.cpp's src/decimate_qem.cpp (which is compiled under
// the name decimate_qem_orig and used for the rare env switches this one does not do).
//
// Same algorithm and same threshold ladder as the original (CuMesh's simplify.cu: Garland-Heckbert quadrics, edge
// length and skinny-triangle penalties, flip rejection, independent-set collapse), with the round made parallel:
//  - the unique edges come from each vertex's own neighbour list (sorted, run-length counted) instead of one big
//    unordered_map, which is where a single thread spent most of its time;
//  - the quadrics, the collapse costs, the propagate step (an atomic min) and the winner test run on all the
//    CPUs the title may use.
// The edge order differs from the original's hash-map order, which was arbitrary anyway; ties between equal costs
// are broken by that order, so the result is another valid simplification of the same quality, not the same file.
// On the console this takes the 2.3 M face lantern to 150 k faces in seconds instead of more than a minute.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include "ps5mg_threads.h"
#include "uv_bake.h"

namespace trellis {

// The original (src/decimate_qem.cpp, built as decimate_qem_orig): used when one of its env switches is set.
void decimate_qem_orig(const std::vector<float> & verts, int V, const std::vector<int32_t> & faces, int F, int target_faces,
                       std::vector<float> & ov, std::vector<int32_t> & of);

namespace {

struct V3 {
    float x, y, z;
};
inline V3    sub(const V3 & a, const V3 & b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V3    cross(const V3 & a, const V3 & b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float dot(const V3 & a, const V3 & b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float nrm2(const V3 & a) { return dot(a, a); }

struct QEM {
    float e[10];
    void  zero() { for (int i = 0; i < 10; ++i) e[i] = 0.f; }
    void  add_plane(float a, float b, float c, float d) {
        e[0] += a * a; e[1] += a * b; e[2] += a * c; e[3] += a * d;
        e[4] += b * b; e[5] += b * c; e[6] += b * d;
        e[7] += c * c; e[8] += c * d; e[9] += d * d;
    }
    float evaluate(float x, float y, float z) const {
        return e[0] * x * x + 2 * e[1] * x * y + 2 * e[2] * x * z + 2 * e[3] * x + e[4] * y * y + 2 * e[5] * y * z +
               2 * e[6] * y + e[7] * z * z + 2 * e[8] * z + e[9];
    }
};

inline uint64_t pack_cost(int id, float c) {
    uint32_t b;
    std::memcpy(&b, &c, 4);
    return ((uint64_t) b << 32) | (uint32_t) id;
}

unsigned thread_count() { return std::max(1u, ps5mg_threads()); }

// fn(begin, end) over [0, n) in blocks, on all threads; blocks are handed out in order.
template <class Fn>
void parallel_blocks(long n, long block, Fn fn) {
    const long nblocks = (n + block - 1) / block;
    if (nblocks <= 0) return;
    std::atomic<long> next{ 0 };
    auto              work = [&]() {
        for (;;) {
            const long b = next.fetch_add(1);
            if (b >= nblocks) break;
            fn(b * block, std::min(n, (b + 1) * block));
        }
    };
    const unsigned nt = (unsigned) std::min<long>(thread_count(), nblocks);
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < nt; t++) pool.emplace_back(work);
    work();
    for (auto & th : pool) th.join();
}

void simplify_round(std::vector<float> & verts, int & V, std::vector<int32_t> & faces, int & F, float lam_len,
                    float lam_skinny, float thresh) {
    auto vat = [&](int i) -> V3 { return { verts[3 * i], verts[3 * i + 1], verts[3 * i + 2] }; };

    // vertex -> incident face adjacency (CSR)
    std::vector<int> off(V + 1, 0);
    for (int f = 0; f < F; ++f)
        for (int k = 0; k < 3; ++k) off[faces[3 * f + k] + 1]++;
    for (int i = 0; i < V; ++i) off[i + 1] += off[i];
    std::vector<int> v2f(off[V]);
    {
        std::vector<int> cur(off.begin(), off.end() - 1);
        for (int f = 0; f < F; ++f)
            for (int k = 0; k < 3; ++k) v2f[cur[faces[3 * f + k]]++] = f;
    }

    // unique undirected edges (a < b) from each vertex's own list of neighbours above it; an edge used by one
    // face is a boundary edge. Blocks of vertices are done in parallel and joined in block order.
    constexpr long kVBlock = 2048;
    const long     nvb     = (V + kVBlock - 1) / kVBlock;
    struct EdgeBlock {
        std::vector<uint64_t> key;
        std::vector<uint8_t>  once;
    };
    std::vector<EdgeBlock> eb((size_t) nvb);
    parallel_blocks(V, kVBlock, [&](long b0, long b1) {
        EdgeBlock &      out = eb[(size_t) (b0 / kVBlock)];
        std::vector<int> nb;
        for (long v = b0; v < b1; ++v) {
            nb.clear();
            for (int j = off[v]; j < off[v + 1]; ++j) {
                const int * ff = &faces[3 * v2f[j]];
                for (int k = 0; k < 3; ++k)
                    if (ff[k] > v) nb.push_back(ff[k]);
            }
            std::sort(nb.begin(), nb.end());
            for (size_t i = 0; i < nb.size();) {
                size_t j = i + 1;
                while (j < nb.size() && nb[j] == nb[i]) ++j;
                out.key.push_back(((uint64_t) (uint32_t) v << 32) | (uint32_t) nb[i]);
                out.once.push_back(j - i == 1);
                i = j;
            }
        }
    });
    std::vector<size_t> eoff((size_t) nvb + 1, 0);
    for (long b = 0; b < nvb; ++b) eoff[(size_t) b + 1] = eoff[(size_t) b] + eb[(size_t) b].key.size();
    const int             E = (int) eoff[(size_t) nvb];
    std::vector<uint64_t> edges((size_t) E);
    std::vector<uint8_t>  boundary((size_t) V, 0);
    for (long b = 0; b < nvb; ++b) {
        const auto & blk = eb[(size_t) b];
        std::copy(blk.key.begin(), blk.key.end(), edges.begin() + (long) eoff[(size_t) b]);
        for (size_t i = 0; i < blk.key.size(); ++i)
            if (blk.once[i]) {
                boundary[(int) (blk.key[i] >> 32)] = 1;
                boundary[(int) (blk.key[i] & 0xffffffffu)] = 1;
            }
    }
    eb.clear();
    eb.shrink_to_fit();

    // per-vertex QEM = sum of incident face plane quadrics (normalized normals)
    std::vector<QEM> qem((size_t) V);
    parallel_blocks(V, 4096, [&](long b0, long b1) {
        for (long i = b0; i < b1; ++i) {
            qem[i].zero();
            for (int j = off[i]; j < off[i + 1]; ++j) {
                const int f = v2f[j];
                V3        a = vat(faces[3 * f]), b = vat(faces[3 * f + 1]), c = vat(faces[3 * f + 2]);
                V3        n  = cross(sub(b, a), sub(c, a));
                const float ln = std::sqrt(nrm2(n));
                if (ln > 1e-20f) { n.x /= ln; n.y /= ln; n.z /= ln; }
                qem[i].add_plane(n.x, n.y, n.z, -dot(n, a));
            }
        }
    });

    // shape-metric accumulation over the faces incident to `keep` (those that go away with `other` are skipped);
    // false if any face would flip.
    auto process = [&](int keep, int other, const V3 & vn, float & skinny, int & ntri) -> bool {
        for (int j = off[keep]; j < off[keep + 1]; ++j) {
            const int f = v2f[j];
            const int x = faces[3 * f], y = faces[3 * f + 1], z = faces[3 * f + 2];
            if (x == other || y == other || z == other) continue;
            V3 a = vat(x), b = vat(y), c = vat(z);
            V3 na = (x == keep) ? vn : a, nb = (y == keep) ? vn : b, nc = (z == keep) ? vn : c;
            V3 on = cross(sub(b, a), sub(c, a));
            V3 ne1 = sub(nb, na), ne2 = sub(nc, na), nn = cross(ne1, ne2);
            if (dot(on, nn) < 0.f) return false;  // flip
            const float narea = 0.5f * std::sqrt(nrm2(nn));
            float       denom = nrm2(sub(nc, nb)) + nrm2(ne1) + nrm2(ne2);
            if (denom < 1e-12f) denom = 1e-12f;
            const float sm = 4.0f * 1.7320508f * narea / denom;  // 1 = equilateral, 0 = degenerate
            skinny += 1.0f - std::min(std::max(sm, 0.0f), 1.0f);
            ntri++;
        }
        return true;
    };

    // edge collapse cost + collapse target
    std::vector<float> cost((size_t) E);
    std::vector<V3>    vnew((size_t) E);
    parallel_blocks(E, 8192, [&](long b0, long b1) {
        for (long t = b0; t < b1; ++t) {
            const int e0 = (int) (edges[t] >> 32), e1 = (int) (edges[t] & 0xffffffffu);
            V3        v0 = vat(e0), v1 = vat(e1);
            float     w0 = 0.5f;
            if (boundary[e0] && !boundary[e1]) w0 = 1.0f;
            else if (!boundary[e0] && boundary[e1]) w0 = 0.0f;
            const V3    v   = { v0.x * w0 + v1.x * (1 - w0), v0.y * w0 + v1.y * (1 - w0), v0.z * w0 + v1.z * (1 - w0) };
            vnew[t]         = v;
            const float el2 = nrm2(sub(v1, v0));
            const QEM   sum = { { qem[e0].e[0] + qem[e1].e[0], qem[e0].e[1] + qem[e1].e[1], qem[e0].e[2] + qem[e1].e[2],
                                  qem[e0].e[3] + qem[e1].e[3], qem[e0].e[4] + qem[e1].e[4], qem[e0].e[5] + qem[e1].e[5],
                                  qem[e0].e[6] + qem[e1].e[6], qem[e0].e[7] + qem[e1].e[7], qem[e0].e[8] + qem[e1].e[8],
                                  qem[e0].e[9] + qem[e1].e[9] } };
            float       c   = sum.evaluate(v.x, v.y, v.z);
            c += lam_len * el2;
            float skinny = 0.f;
            int   ntri   = 0;
            if (!(process(e0, e1, v, skinny, ntri) && process(e1, e0, v, skinny, ntri))) {
                cost[t] = std::numeric_limits<float>::infinity();
                continue;
            }
            if (ntri > 0) skinny /= ntri;
            c += lam_skinny * skinny * el2;
            cost[t] = c;
        }
    });

    // propagate each edge's (cost,id) to all faces touching either endpoint (min-reduce, as an atomic min)
    std::vector<std::atomic<uint64_t>> prop((size_t) F);
    parallel_blocks(F, 1 << 16, [&](long b0, long b1) {
        for (long f = b0; f < b1; ++f) prop[f].store(UINT64_MAX, std::memory_order_relaxed);
    });
    parallel_blocks(E, 8192, [&](long b0, long b1) {
        for (long t = b0; t < b1; ++t) {
            const uint64_t p  = pack_cost((int) t, cost[t]);
            const int      e0 = (int) (edges[t] >> 32), e1 = (int) (edges[t] & 0xffffffffu);
            for (const int e : { e0, e1 })
                for (int j = off[e]; j < off[e + 1]; ++j) {
                    std::atomic<uint64_t> & slot = prop[v2f[j]];
                    uint64_t                cur  = slot.load(std::memory_order_relaxed);
                    while (p < cur && !slot.compare_exchange_weak(cur, p, std::memory_order_relaxed)) {}
                }
        }
    });

    // collapse edges that win every incident face and are under threshold (in edge order, blocks joined in order)
    const long                    neb = (E + 8191) / 8192;
    std::vector<std::vector<int>> wblock((size_t) std::max(1L, neb));
    parallel_blocks(E, 8192, [&](long b0, long b1) {
        std::vector<int> & out = wblock[(size_t) (b0 / 8192)];
        for (long t = b0; t < b1; ++t) {
            if (!(cost[t] <= thresh)) continue;
            const uint64_t p  = pack_cost((int) t, cost[t]);
            const int      e0 = (int) (edges[t] >> 32), e1 = (int) (edges[t] & 0xffffffffu);
            bool           own = true;
            for (int j = off[e0]; j < off[e0 + 1] && own; ++j)
                if (prop[v2f[j]].load(std::memory_order_relaxed) != p) own = false;
            for (int j = off[e1]; j < off[e1 + 1] && own; ++j)
                if (prop[v2f[j]].load(std::memory_order_relaxed) != p) own = false;
            if (own) out.push_back((int) t);
        }
    });
    std::vector<int> winners;
    for (auto & w : wblock) winners.insert(winners.end(), w.begin(), w.end());

    std::vector<uint8_t> vdead((size_t) V, 0), fdead((size_t) F, 0);
    for (const int t : winners) {
        const int e0 = (int) (edges[t] >> 32), e1 = (int) (edges[t] & 0xffffffffu);
        verts[3 * e0] = vnew[t].x; verts[3 * e0 + 1] = vnew[t].y; verts[3 * e0 + 2] = vnew[t].z;
        vdead[e1] = 1;
        for (int j = off[e0]; j < off[e0 + 1]; ++j) {
            const int f = v2f[j];
            int *     ff = &faces[3 * f];
            if (ff[0] == e1 || ff[1] == e1 || ff[2] == e1) fdead[f] = 1;
        }
        for (int j = off[e1]; j < off[e1 + 1]; ++j) {
            const int f = v2f[j];
            int *     ff = &faces[3 * f];
            for (int k = 0; k < 3; ++k)
                if (ff[k] == e1) ff[k] = e0;
        }
    }

    // compact vertices (drop collapsed) + faces (drop deleted/degenerate)
    std::vector<int> vmap((size_t) V, -1);
    int              nV = 0;
    for (int i = 0; i < V; ++i)
        if (!vdead[i]) vmap[i] = nV++;
    std::vector<float> nv((size_t) nV * 3);
    for (int i = 0; i < V; ++i)
        if (vmap[i] >= 0) { nv[3 * vmap[i]] = verts[3 * i]; nv[3 * vmap[i] + 1] = verts[3 * i + 1]; nv[3 * vmap[i] + 2] = verts[3 * i + 2]; }
    std::vector<int32_t> nf;
    nf.reserve(faces.size());
    for (int f = 0; f < F; ++f) {
        if (fdead[f]) continue;
        const int a = vmap[faces[3 * f]], b = vmap[faces[3 * f + 1]], c = vmap[faces[3 * f + 2]];
        if (a < 0 || b < 0 || c < 0 || a == b || b == c || a == c) continue;
        nf.push_back(a); nf.push_back(b); nf.push_back(c);
    }
    V = nV;
    F = (int) nf.size() / 3;
    verts.swap(nv);
    faces.swap(nf);
}

}  // namespace

void decimate_qem(const std::vector<float> & in_verts, int V0, const std::vector<int32_t> & in_faces, int F0, int target_faces,
                  std::vector<float> & ov, std::vector<int32_t> & of) {
    if (std::getenv("TRELLIS_QEM_PRESERVE_TOPOLOGY") || std::getenv("TRELLIS_QEM_MAX_NORMAL_CHANGE_DEG") ||
        std::getenv("TRELLIS_QEM_COLLISION_GUARD")) {
        decimate_qem_orig(in_verts, V0, in_faces, F0, target_faces, ov, of);
        return;
    }
    std::vector<float>   verts = in_verts;
    std::vector<int32_t> faces = in_faces;
    int                  V = V0, F = F0;
    if (F <= target_faces) {
        ov = verts;
        of = faces;
        return;
    }
    float       thresh = 1e-8f;
    const float lam_len = 1e-2f, lam_skinny = 1e-3f;
    int         prevF = F, stalls = 0;
    for (int round = 0; round < 400 && F > target_faces; ++round) {
        simplify_round(verts, V, faces, F, lam_len, lam_skinny, thresh);
        if (F <= target_faces) break;
        const int removed = prevF - F;
        if (removed <= 0) {
            if (++stalls >= 2) { thresh *= 10.0f; stalls = 0; }
        } else {
            stalls = 0;
            if ((float) removed / prevF < 1e-2f) thresh *= 10.0f;
        }
        prevF = F;
        if (thresh > 1e12f) break;  // fully collapsed within tolerance; stop escalating
    }

    // final compaction to referenced vertices only (drop any isolated leftovers)
    std::vector<int> used((size_t) V, -1);
    int              nV = 0;
    for (int f = 0; f < F; ++f)
        for (int k = 0; k < 3; ++k) {
            const int v = faces[3 * f + k];
            if (used[v] < 0) used[v] = nV++;
        }
    ov.assign((size_t) nV * 3, 0.f);
    for (int i = 0; i < V; ++i)
        if (used[i] >= 0) { ov[3 * used[i]] = verts[3 * i]; ov[3 * used[i] + 1] = verts[3 * i + 1]; ov[3 * used[i] + 2] = verts[3 * i + 2]; }
    of.resize((size_t) F * 3);
    for (int f = 0; f < F; ++f)
        for (int k = 0; k < 3; ++k) of[3 * f + k] = used[faces[3 * f + k]];
    std::printf("  decimate_qem(target=%d): V %d->%d, F %d->%d (thresh=%.1e)\n", target_faces, V0, nV, F0, F, (double) thresh);
    std::fflush(stdout);
}

}  // namespace trellis
