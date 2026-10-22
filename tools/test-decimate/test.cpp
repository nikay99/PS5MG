// decimate_qem_fast.cpp against trellis.cpp's original decimate_qem.cpp on a real mesh (int V, int F, float[V*3], int[F*3]).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include "uv_bake.h"
namespace trellis {
void decimate_qem_orig(const std::vector<float>&, int, const std::vector<int32_t>&, int, int, std::vector<float>&, std::vector<int32_t>&);
}
struct Stats { long open_edges = 0, nonmanifold = 0; double volume = 0, bb[6]; };
static Stats stats(const std::vector<float>& v, const std::vector<int32_t>& f) {
    Stats s; for (int k = 0; k < 3; k++) { s.bb[k] = 1e30; s.bb[3+k] = -1e30; }
    for (size_t i = 0; i < v.size() / 3; i++) for (int k = 0; k < 3; k++) { s.bb[k] = std::min<double>(s.bb[k], v[3*i+k]); s.bb[3+k] = std::max<double>(s.bb[3+k], v[3*i+k]); }
    std::unordered_map<uint64_t,int> ec; ec.reserve(f.size());
    auto key = [](int a, int b) { if (a > b) std::swap(a, b); return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; };
    for (size_t t = 0; t < f.size() / 3; t++) {
        int a = f[3*t], b = f[3*t+1], c = f[3*t+2];
        ec[key(a,b)]++; ec[key(b,c)]++; ec[key(c,a)]++;
        const float *pa = &v[3*a], *pb = &v[3*b], *pc = &v[3*c];
        s.volume += (pa[0]*(pb[1]*pc[2]-pb[2]*pc[1]) - pa[1]*(pb[0]*pc[2]-pb[2]*pc[0]) + pa[2]*(pb[0]*pc[1]-pb[1]*pc[0])) / 6.0;
    }
    for (auto& kv : ec) { if (kv.second == 1) s.open_edges++; else if (kv.second > 2) s.nonmanifold++; }
    return s;
}
int main(int argc, char** argv) {
    FILE* fp = fopen(argv[1], "rb"); int V, F; fread(&V, 4, 1, fp); fread(&F, 4, 1, fp);
    std::vector<float> v((size_t)V*3); std::vector<int32_t> f((size_t)F*3);
    fread(v.data(), 4, v.size(), fp); fread(f.data(), 4, f.size(), fp); fclose(fp);
    int target = argc > 2 ? atoi(argv[2]) : 150000;
    std::vector<float> ov1, ov2; std::vector<int32_t> of1, of2;
    auto t0 = std::chrono::steady_clock::now();
    if (argc < 4) trellis::decimate_qem_orig(v, V, f, F, target, ov1, of1);
    auto t1 = std::chrono::steady_clock::now();
    trellis::decimate_qem(v, V, f, F, target, ov2, of2);
    auto t2 = std::chrono::steady_clock::now();
    auto in = stats(v, f);
    std::printf("input : V %d F %d open %ld volume %.5f\n", V, F, in.open_edges, in.volume);
    if (argc < 4) { auto a = stats(ov1, of1); std::printf("orig  : %.1f s  V %zu F %zu open %ld nonmf %ld volume %.5f\n", std::chrono::duration<double>(t1-t0).count(), ov1.size()/3, of1.size()/3, a.open_edges, a.nonmanifold, a.volume); }
    auto b = stats(ov2, of2);
    std::printf("fast  : %.1f s  V %zu F %zu open %ld nonmf %ld volume %.5f\n", std::chrono::duration<double>(t2-t1).count(), ov2.size()/3, of2.size()/3, b.open_edges, b.nonmanifold, b.volume);
    FILE* o = fopen("fast.bin", "wb"); int nv = ov2.size()/3, nf = of2.size()/3; fwrite(&nv,4,1,o); fwrite(&nf,4,1,o); fwrite(ov2.data(),4,ov2.size(),o); fwrite(of2.data(),4,of2.size(),o); fclose(o);
    return 0;
}
