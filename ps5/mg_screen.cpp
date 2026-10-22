// PS5MG's screen: PS5SD's dashboard (ps5/screen, adapted from PS5LM and PS5SD, GPL-3.0) fed from the job state of
// mg_main.cpp instead of from stable-diffusion.cpp. The input picture of the running job is the "preview", the
// pictures of finished jobs are the "recent" strip.
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "app_status.hpp"
#include "dashboard.hpp"
#include "display.hpp"
#include "gfx/renderer.hpp"
#include "image_file.hpp"
#include "mg_state.hpp"

extern "C" {
int sceKernelUsleep(unsigned int microseconds);
}

namespace {

constexpr const char * kFont    = "/app0/assets/fonts/dejavu-sans-mono.huifont";
constexpr const char * kVersion = "0.1.1";
constexpr int          kWebPort = 7870;

ps5sd::AppStatus g_status;
ps5sd::Display   g_display;
hui::gfx::Renderer g_renderer;
ps5sd::Dashboard g_view;
bool             g_ui_ok = false;

std::string slurp(const std::string & p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The console's address on the local network (PS5SD's lan_address): a UDP socket "connected" to an outside
// address sends nothing, and getsockname() names the interface it would leave by.
std::string lan_address() {
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return {};
    sockaddr_in to     = {};
    to.sin_family      = AF_INET;
    to.sin_port        = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808);
    sockaddr_in local  = {};
    socklen_t   len    = sizeof(local);
    std::string out;
    if (connect(s, reinterpret_cast<const sockaddr *>(&to), sizeof(to)) == 0 &&
        getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0) {
        const uint32_t a = ntohl(local.sin_addr.s_addr);
        if (a != 0 && (a >> 24) != 127) {
            char text[16];
            std::snprintf(text, sizeof(text), "%u.%u.%u.%u", a >> 24, (a >> 16) & 255u, (a >> 8) & 255u, a & 255u);
            out = text;
        }
    }
    close(s);
    return out;
}

std::string job_label(const std::string & id) {
    const std::string meta = slurp(std::string(kMgJobs) + "/" + id + ".meta");
    const std::string key  = "\"label\":\"";
    const size_t      at   = meta.find(key);
    if (at != std::string::npos) {
        const size_t end = meta.find('"', at + key.size());
        if (end != std::string::npos && end > at + key.size()) return meta.substr(at + key.size(), end - at - key.size());
    }
    return id;
}

}  // namespace

bool mg_screen_open() {
    // The screen first: it takes from the same memory pool as the models.
    if (!g_display.open(1920, 1080)) {
        std::printf("ps5mg: no display\n");
        return false;
    }
    g_ui_ok = g_renderer.init() && g_view.load(g_renderer, kFont);
    std::printf("ps5mg: screen %s\n", g_ui_ok ? "ready" : "failed");
    g_status.set_model("TRELLIS.2 (q4)");
    g_status.set_port(kWebPort);
    g_status.set_phase("Opening the GPU");
    return g_ui_ok;
}

// One frame; the caller loops (the main thread, until the title ends).
void mg_screen_frame() {
    using clock = std::chrono::steady_clock;
    static auto        last   = clock::now();
    static std::string shown_job;
    static size_t      shown_tail = 0;
    static std::set<std::string> seen;
    static double      next_scan = 0, next_addr = 0;
    static std::string address;

    // What the worker says (a copy: the mutex is held briefly).
    std::string job, stage, progress;
    std::vector<std::string> tail;
    double free_gib, pool_gib;
    {
        std::lock_guard<std::mutex> lk(g_mg.mu);
        job = g_mg.job; stage = g_mg.stage; progress = g_mg.progress; tail = g_mg.tail;
        free_gib = g_mg.pool_free_gib; pool_gib = g_mg.pool_gib;
    }
    g_status.set_memory(free_gib, pool_gib);

    if (mg_now() >= next_addr) {
        address   = lan_address();
        next_addr = mg_now() + (address.empty() ? 5 : 60);
        g_status.set_address(address);
    }

    // Stage n of N, and the flow bar's step: one progress figure over the whole job.
    int stage_n = 0, stage_total = 7;
    if (stage.size() > 4 && stage[0] == '[') std::sscanf(stage.c_str(), "[%d/%d]", &stage_n, &stage_total);
    if (stage_total < 1) stage_total = 7;
    double frac = 0;
    if (!job.empty()) {
        frac = stage_n > 0 ? (stage_n - 1.0) / stage_total : 0.0;
        const size_t close = progress.rfind(']');
        int          a = 0, b = 0;
        if (close != std::string::npos && std::sscanf(progress.c_str() + close + 1, " %d/%d", &a, &b) == 2 && b > 0) {
            frac += (static_cast<double>(a) / b) / stage_total;
        }
    }

    static std::string last_phase, last_label;
    static std::vector<uint8_t> preview_rgb;
    static int preview_w = 0, preview_h = 0;
    if (!job.empty()) {
        if (job != shown_job) {
            shown_job = job;
            // The input picture as the live preview.
            ps5sd::Rgba img;
            preview_rgb.clear();
            if (ps5sd::load_image_rgba(std::string(kMgIn) + "/" + job + ".png", 512, img) && img.width > 0) {
                preview_w = img.width;
                preview_h = img.height;
                preview_rgb.resize((size_t) img.width * img.height * 3);
                for (size_t i = 0; i < (size_t) img.width * img.height; i++) {
                    preview_rgb[i * 3]     = img.pixels[i * 4];
                    preview_rgb[i * 3 + 1] = img.pixels[i * 4 + 1];
                    preview_rgb[i * 3 + 2] = img.pixels[i * 4 + 2];
                }
            }
            last_phase.clear();
        }
        // The stage as the second line of the phase. set_phase starts a job afresh (drops the preview), so it
        // is only called when the text changes, and the preview is put back after it.
        std::string text = stage.size() > 6 && stage[0] == '[' ? stage.substr(stage.find(']') + 2) : stage;
        if (text.size() > 60) text.resize(60);
        last_label = job_label(job);
        const std::string phase = last_label + "  -  " + text;
        if (phase != last_phase) {
            last_phase = phase;
            g_status.set_phase("Generating", phase);
            if (!preview_rgb.empty()) g_status.set_preview(preview_rgb.data(), preview_w, preview_h);
        }
        double job_s = 0;
        {
            std::lock_guard<std::mutex> lk(g_mg.mu);
            job_s = mg_now() - g_mg.job_t0;
        }
        g_status.set_progress(static_cast<int>(frac * 1000), 1000, frac > 0.01 ? static_cast<float>(job_s / (frac * 1000)) : 0.f);
    } else {
        shown_job.clear();
        if (last_phase != "Ready") {
            last_phase = "Ready";
            // The last picture's caption is the job's name alone, not the stage it ended in.
            if (!last_label.empty()) g_status.set_phase("Generating", last_label);
            g_status.set_phase("Ready");
        }
    }

    // The log tail: lines the dashboard has not seen.
    if (tail.size() < shown_tail) shown_tail = 0;
    for (size_t i = shown_tail; i < tail.size(); i++) g_status.log(tail[i]);
    shown_tail = tail.size();

    // Finished jobs: their pictures, newest first, once each.
    if (mg_now() >= next_scan) {
        next_scan = mg_now() + 4;
        std::vector<std::string> done;
        if (DIR * d = opendir(kMgJobs)) {
            while (dirent * e = readdir(d)) {
                const std::string n = e->d_name;
                if (n.size() > 5 && n.compare(n.size() - 5, 5, ".done") == 0) done.push_back(n.substr(0, n.size() - 5));
            }
            closedir(d);
        }
        std::sort(done.begin(), done.end());
        for (const auto & id : done) {
            const std::string img = std::string(kMgIn) + "/" + id + ".png";
            struct stat st;
            if (!seen.count(id) && stat(img.c_str(), &st) == 0) {
                seen.insert(id);
                g_status.set_result(img);
            } else {
                seen.insert(id);
            }
        }
    }

    const auto now = clock::now();
    g_view.update(g_status.snapshot(), std::chrono::duration<float>(now - last).count());
    last = now;
    g_view.compose(g_renderer, kVersion);
    g_display.present(g_renderer);
    if (!g_display.swap()) sceKernelUsleep(16 * 1000);
    // While a job runs the screen redraws about 8 times a second: at 60 the GL driver took 10 percent of the GPU's
    // time from the flows (measured: structure flow 49 s with the screen at full rate, 45 s without it).
    if (!job.empty()) sceKernelUsleep(110 * 1000);
}
