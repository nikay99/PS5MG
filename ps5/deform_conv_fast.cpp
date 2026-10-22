// PS5MG's modulated deformable conv2d for BiRefNet's ASPPDeformable, in place of trellis.cpp's
// src/deform_conv_cpu.cpp (same two functions, same layout and border rule).
//
// The original samples the input again for every output channel and input channel. This one samples once per
// output pixel and tap, into a channel-last column block, and then multiplies that with the weights: the
// sampling costs Cin*K*K*HW instead of Cout*Cin*K*K*HW, and the rest is a plain matrix product that AVX2 FMA
// does well. On the PS5 this is the difference between minutes and seconds for the background removal.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "deform_conv.h"
#include "ps5mg_threads.h"

#if defined(__AVX2__) && defined(__FMA__)
#include <immintrin.h>
#endif

namespace trellis {

namespace {

// sum_i a[i]*b[i]
inline float dot(const float * a, const float * b, int n) {
    int   i   = 0;
    float sum = 0.f;
#if defined(__AVX2__) && defined(__FMA__)
    __m256 acc0 = _mm256_setzero_ps(), acc1 = _mm256_setzero_ps();
    for (; i + 16 <= n; i += 16) {
        acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), acc0);
        acc1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8), acc1);
    }
    acc0 = _mm256_add_ps(acc0, acc1);
    __m128 lo = _mm_add_ps(_mm256_castps256_ps128(acc0), _mm256_extractf128_ps(acc0, 1));
    lo        = _mm_add_ps(lo, _mm_movehl_ps(lo, lo));
    lo        = _mm_add_ss(lo, _mm_shuffle_ps(lo, lo, 1));
    sum       = _mm_cvtss_f32(lo);
#endif
    for (; i < n; i++) sum += a[i] * b[i];
    return sum;
}

}  // namespace

void deform_conv2d_cpu(const float * x, int Cin, int H, int W, const float * offset, const float * mask,
                       const float * weight, const float * bias, int Cout, int K, float * out, int /*gpu*/) {
    const long HW   = (long) H * W;
    const int  K2   = K * K;
    const int  pad  = K / 2;
    const bool log_timing = std::getenv("TRELLIS_DBG_DEFORM") != nullptr;
    const auto t0 = std::chrono::steady_clock::now();

    // x as [HW][Cin]: the four corners of a sample are then four contiguous channel vectors.
    std::vector<float> xt((size_t) HW * Cin);
    for (int c = 0; c < Cin; c++) {
        const float * src = x + (long) c * HW;
        for (long p = 0; p < HW; p++) xt[(size_t) p * Cin + c] = src[p];
    }
    // weight as [t][oc][ic], so that for one tap the dot product over ic runs over contiguous memory.
    std::vector<float> wt((size_t) K2 * Cout * Cin);
    for (int oc = 0; oc < Cout; oc++)
        for (int ic = 0; ic < Cin; ic++)
            for (int t = 0; t < K2; t++) wt[((size_t) t * Cout + oc) * Cin + ic] = weight[((long) oc * Cin + ic) * K2 + t];

    constexpr int TP = 16;  // output pixels per block: K2*TP*Cin floats of columns stay in cache
    const long    nblocks = (HW + TP - 1) / TP;
    std::atomic<long> next{0};

    auto worker = [&]() {
        std::vector<float> col((size_t) K2 * TP * Cin);
        for (;;) {
            const long b = next.fetch_add(1);
            if (b >= nblocks) break;
            const long p0 = b * TP, np = std::min<long>(TP, HW - p0);
            for (int t = 0; t < K2; t++) {
                const int kh = t / K, kw = t % K;
                for (int i = 0; i < np; i++) {
                    const long  rem = p0 + i;
                    const int   oy = (int) (rem / W), ox = (int) (rem % W);
                    float *     dst = &col[((size_t) t * TP + i) * Cin];
                    const float oh = offset[(long) (2 * t) * HW + rem], ow = offset[(long) (2 * t + 1) * HW + rem];
                    const float m  = mask[(long) t * HW + rem];
                    const float h  = (float) (oy - pad + kh) + oh, w = (float) (ox - pad + kw) + ow;
                    // same border rule as torchvision / the original: outside (-1,H) x (-1,W) is zero, and a
                    // corner outside the image counts as zero.
                    if (h <= -1.f || (float) H <= h || w <= -1.f || (float) W <= w) {
                        std::fill(dst, dst + Cin, 0.f);
                        continue;
                    }
                    const int   hl = (int) std::floor(h), wl = (int) std::floor(w), hh = hl + 1, wh = wl + 1;
                    const float lh = h - hl, lw = w - wl, hh_ = 1.f - lh, hw_ = 1.f - lw;
                    const float c1 = (hl >= 0 && wl >= 0) ? m * hh_ * hw_ : 0.f;
                    const float c2 = (hl >= 0 && wh <= W - 1) ? m * hh_ * lw : 0.f;
                    const float c3 = (hh <= H - 1 && wl >= 0) ? m * lh * hw_ : 0.f;
                    const float c4 = (hh <= H - 1 && wh <= W - 1) ? m * lh * lw : 0.f;
                    const float * p1 = c1 != 0.f ? &xt[((size_t) hl * W + wl) * Cin] : nullptr;
                    const float * p2 = c2 != 0.f ? &xt[((size_t) hl * W + wh) * Cin] : nullptr;
                    const float * p3 = c3 != 0.f ? &xt[((size_t) hh * W + wl) * Cin] : nullptr;
                    const float * p4 = c4 != 0.f ? &xt[((size_t) hh * W + wh) * Cin] : nullptr;
                    for (int c = 0; c < Cin; c++) {
                        float v = 0.f;
                        if (p1) v += c1 * p1[c];
                        if (p2) v += c2 * p2[c];
                        if (p3) v += c3 * p3[c];
                        if (p4) v += c4 * p4[c];
                        dst[c] = v;
                    }
                }
            }
            for (int oc = 0; oc < Cout; oc++) {
                for (int i = 0; i < np; i++) {
                    float acc = bias ? bias[oc] : 0.f;
                    for (int t = 0; t < K2; t++)
                        acc += dot(&col[((size_t) t * TP + i) * Cin], &wt[((size_t) t * Cout + oc) * Cin], Cin);
                    out[(long) oc * HW + p0 + i] = acc;
                }
            }
        }
    };

    const unsigned    hw_threads = ps5mg_threads();
    const int         nthreads   = (int) std::max<long>(1, std::min<long>(nblocks, hw_threads ? (long) hw_threads : 4));
    std::vector<std::thread> pool;
    for (int t = 1; t < nthreads; t++) pool.emplace_back(worker);
    worker();
    for (auto & th : pool) th.join();

    if (log_timing) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[deform_fast] Cin=%d Cout=%d H=%d W=%d K=%d -> %.1f ms\n", Cin, Cout, H, W, K, ms);
    }
}

void deform_conv2d_run(const float * x, int Cin, int H, int W, const float * offset, const float * mask,
                       const float * weight, const float * bias, int Cout, int K, float * out, int gpu) {
    deform_conv2d_cpu(x, Cin, H, W, offset, mask, weight, bias, Cout, K, out, gpu);
}

}  // namespace trellis
