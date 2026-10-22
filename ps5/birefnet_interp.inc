// bilinear, align_corners=True: PS5MG's version of trellis.cpp's interp() (src/birefnet.cpp). Same samples, but
// channel by channel (the original walked all channels for every pixel, across planes that do not fit the cache),
// in float, and on all the CPUs the title may use. The decoder's last step, 192 channels to 1024 x 1024, took 25 s.
static Feat interp(const Feat& x, int Ho, int Wo) {
    Feat o; o.C = x.C; o.H = Ho; o.W = Wo; o.d.resize((size_t)x.C * Ho * Wo);
    const double sy = Ho > 1 ? (double)(x.H - 1) / (Ho - 1) : 0.0, sx = Wo > 1 ? (double)(x.W - 1) / (Wo - 1) : 0.0;
    std::vector<int> xa(Wo), xb(Wo); std::vector<float> lxv(Wo);
    for (int wo = 0; wo < Wo; ++wo) { double fx = wo * sx; int x0 = (int)fx; xa[wo] = x0; xb[wo] = std::min(x0 + 1, x.W - 1); lxv[wo] = (float)(fx - x0); }
    std::atomic<int> next{0};
    auto work = [&]() {
        for (;;) {
            const int ch = next.fetch_add(1);
            if (ch >= x.C) break;
            const float* s = &x.d[(size_t)ch * x.H * x.W];
            float* d = &o.d[(size_t)ch * Ho * Wo];
            for (int ho = 0; ho < Ho; ++ho) {
                const double fy = ho * sy; const int y0 = (int)fy, y1 = std::min(y0 + 1, x.H - 1); const float ly = (float)(fy - y0);
                const float* r0 = s + (size_t)y0 * x.W; const float* r1 = s + (size_t)y1 * x.W;
                float* dr = d + (size_t)ho * Wo;
                for (int wo = 0; wo < Wo; ++wo) {
                    const float lx = lxv[wo]; const int a = xa[wo], b = xb[wo];
                    dr[wo] = (1 - ly) * ((1 - lx) * r0[a] + lx * r0[b]) + ly * ((1 - lx) * r1[a] + lx * r1[b]);
                }
            }
        }
    };
    const unsigned nt = std::max(1u, std::min<unsigned>(ps5mg_threads(), (unsigned)x.C));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < nt; ++t) pool.emplace_back(work);
    work();
    for (auto& th : pool) th.join();
    return o;
}
