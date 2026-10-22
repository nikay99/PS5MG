// deform_conv_fast.cpp against trellis.cpp's original CPU kernel: same numbers, and how much faster.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "deform_conv.h"
namespace trellis {
void deform_conv2d_cpu_ref(const float*, int, int, int, const float*, const float*, const float*, const float*, int, int, float*, int);
}
int main() {
    struct C { int Cin, Cout, H, W; } cases[] = {{16, 24, 20, 17}, {64, 64, 64, 64}, {128, 96, 48, 48}, {256, 64, 128, 128}};
    std::mt19937 g(1);
    std::normal_distribution<float> n(0, 1);
    std::uniform_real_distribution<float> u(0, 2);
    int bad = 0;
    for (auto c : cases) {
        const int K = 3, K2 = 9;
        long HW = (long)c.H * c.W;
        std::vector<float> x(c.Cin * HW), off(2 * K2 * HW), m(K2 * HW), w((long)c.Cout * c.Cin * K2), b(c.Cout), o1(c.Cout * HW), o2(c.Cout * HW);
        for (auto & v : x) v = n(g);
        for (auto & v : off) v = n(g) * 2.5f;  // large offsets: reaches the borders
        for (auto & v : m) v = u(g);
        for (auto & v : w) v = n(g) * 0.05f;
        for (auto & v : b) v = n(g);
        auto t0 = std::chrono::steady_clock::now();
        trellis::deform_conv2d_cpu_ref(x.data(), c.Cin, c.H, c.W, off.data(), m.data(), w.data(), b.data(), c.Cout, K, o1.data(), 0);
        auto t1 = std::chrono::steady_clock::now();
        trellis::deform_conv2d_cpu(x.data(), c.Cin, c.H, c.W, off.data(), m.data(), w.data(), b.data(), c.Cout, K, o2.data(), 0);
        auto t2 = std::chrono::steady_clock::now();
        double maxd = 0, maxv = 0;
        for (size_t i = 0; i < o1.size(); i++) { maxd = std::max(maxd, (double)std::fabs(o1[i] - o2[i])); maxv = std::max(maxv, (double)std::fabs(o1[i])); }
        double ref_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(), fast_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        std::printf("Cin=%3d Cout=%3d %3dx%-3d  ref %8.1f ms  fast %7.1f ms  x%.1f  max|diff| %.2e (max|v| %.1f)\n", c.Cin, c.Cout, c.H, c.W, ref_ms, fast_ms, ref_ms / fast_ms, maxd, maxv);
        if (maxd > 1e-3 * (1 + maxv)) bad++;
    }
    std::puts(bad ? "MISMATCH" : "MATCH");
    return bad;
}
