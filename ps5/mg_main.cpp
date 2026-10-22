// PS5MG spike: runs trellis.cpp's CLI as a headless PS5 title. Startup patterns are PS5SD's
// (ps5/app/main.cpp, adapted from PS5LM, GPL-3.0): Vulkan forwarders, log to a file, quit file.
//
// /data/PS5MG/jobs/<name>.job  one line, the trellis-cli arguments; run in name order, then renamed .done or .failed
// /data/PS5MG/app.log    everything the run prints
// /data/PS5MG/quit       create it and the title exits itself
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "mg_state.hpp"
#include "ps5mg_threads.h"

// No Vulkan loader on the console: the four entry points ggml-vulkan links against go to RADV.
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char * name);
extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char * name) {
    return vk_icdGetInstanceProcAddr(instance, name);
}
extern "C" PFN_vkVoidFunction vk_common_GetDeviceProcAddr(VkDevice device, const char * name);
extern "C" void vk_common_GetPhysicalDeviceFeatures2(VkPhysicalDevice dev, VkPhysicalDeviceFeatures2 * features);
extern "C" void vk_common_CmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t n,
                                        const VkBufferCopy * regions);
extern "C" PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char * name) {
    return vk_common_GetDeviceProcAddr(device, name);
}
extern "C" void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice dev, VkPhysicalDeviceFeatures2 * features) {
    vk_common_GetPhysicalDeviceFeatures2(dev, features);
}
extern "C" void vkCmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t n,
                                const VkBufferCopy * regions) {
    vk_common_CmdCopyBuffer(cmd, src, dst, n, regions);
}

extern "C" {
int       sceSystemServiceHideSplashScreen(void);
int       sceSystemServiceLoadExec(const char * path, const char * const * args);
int64_t   sceKernelGetDirectMemorySize(void);
int       sceKernelAvailableDirectMemorySize(int64_t start, int64_t end, size_t align, int64_t * at, size_t * size);
}

int trellis_cli_main(int argc, char ** argv);
#ifdef MG_SCREEN
bool mg_screen_open();
void mg_screen_frame();
#endif

MgState g_mg;
static std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();
double mg_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count(); }

namespace {
constexpr const char * kDir  = "/data/PS5MG";
constexpr const char * kLog  = "/data/PS5MG/app.log";
constexpr const char * kRaw  = "/data/PS5MG/app.raw";
constexpr const char * kJobs = "/data/PS5MG/jobs";
constexpr const char * kLogs = "/data/PS5MG/logs";
constexpr const char * kQuit = "/data/PS5MG/quit";
constexpr double       kGiB  = 1024.0 * 1024.0 * 1024.0;

std::atomic<bool> g_done{false};
std::atomic<bool> g_restart{false};  // a job ran in this process: the next one gets a fresh one
std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();

// stdout/stderr go to app.raw (freopen, which works on the console); this thread follows that file and writes
// app.log with the seconds since start in front of each line.
void * stamp_log(void *) {
    FILE * out = fopen(kLog, "a");
    FILE * jobout = nullptr;
    std::string jobname;
    FILE * in  = nullptr;
    while (!in) {
        in = fopen(kRaw, "r");
        if (!in) usleep(100000);
    }
    std::string line;
    int         c;
    for (;;) {
        c = fgetc(in);
        if (c == EOF) {
            clearerr(in);
            usleep(100000);
            fflush(stdout);
            continue;
        }
        // Progress bars end their lines with \r: a line ends at \n or \r.
        if (c != '\n' && c != '\r') {
            line += static_cast<char>(c);
            continue;
        }
        // The OpenGL driver of the screen reports its frames ("[ps5-gallium] ...", "[ps5-driver-cycles] ...") every
        // second or so; that is not what the log is for.
        if (line.empty() || line.compare(0, 5, "[ps5-") == 0) {
            line.clear();
            continue;
        }
        std::fprintf(out, "[%7.1f] %s\n", mg_now(), line.c_str());
        std::fflush(out);
        {
            // The running job's own log: /data/PS5MG/logs/<job>.log
            std::string cur;
            {
                std::lock_guard<std::mutex> lk(g_mg.mu);
                cur = g_mg.job;
            }
            if (cur != jobname) {
                if (jobout) fclose(jobout);
                jobname = cur;
                jobout  = cur.empty() ? nullptr : fopen((std::string(kLogs) + "/" + cur + ".log").c_str(), "w");
            }
            if (jobout) {
                std::fprintf(jobout, "[%7.1f] %s\n", mg_now(), line.c_str());
                std::fflush(jobout);
            }
        }
        {
            // What the web interface shows: the stage line ("[3/7] ..."), the progress bar, the last lines.
            std::lock_guard<std::mutex> lk(g_mg.mu);
            if (line.size() > 4 && line[0] == '[' && isdigit(static_cast<unsigned char>(line[1])) && line[2] == '/') {
                g_mg.stage = line;
                g_mg.progress.clear();
            } else if (line.find("[flow] [") != std::string::npos) {
                g_mg.progress = line;
            } else if (line.find("dit-fwd") == std::string::npos && line.find("trellis:dbg") == std::string::npos &&
                       line.compare(0, 5, "radv/") != 0 && line.compare(0, 4, "[Sce") != 0) {
                g_mg.tail.push_back(line.substr(0, 200));
                if (g_mg.tail.size() > 200) g_mg.tail.erase(g_mg.tail.begin(), g_mg.tail.begin() + 100);
            }
        }
        line.clear();
    }
    return nullptr;
}

void log_memory(const char * when) {
    int64_t       at   = 0;
    size_t        free = 0;
    const int64_t pool = sceKernelGetDirectMemorySize();
    sceKernelAvailableDirectMemorySize(0, pool, 2 * 1024 * 1024, &at, &free);
    std::printf("ps5mg: memory %s: pool free %.2f of %.2f GiB", when, free / kGiB, pool / kGiB);
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
        size_t dev_free = 0, dev_total = 0;
        ggml_backend_dev_memory(ggml_backend_dev_get(i), &dev_free, &dev_total);
        std::printf(", %s free %zu total %zu", ggml_backend_dev_name(ggml_backend_dev_get(i)), dev_free, dev_total);
    }
    std::printf("\n");
    std::fflush(stdout);
}

// "bench-read <file> <block bytes>": how fast the console reads a file (first read of it, so not from cache), with
// read() and blocks of that size, for at most 8 seconds.
int bench_read(const std::string & line) {
    std::istringstream words(line);
    std::string        cmd, path;
    size_t             block = 1 << 20;
    words >> cmd >> path >> block;
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::printf("ps5mg: bench-read: cannot open %s\n", path.c_str());
        return 2;
    }
    std::vector<char> buf(block);
    size_t            total = 0;
    const double      t0 = mg_now();
    for (;;) {
        const ssize_t n = read(fd, buf.data(), block);
        if (n <= 0) break;
        total += static_cast<size_t>(n);
        if (mg_now() - t0 > 8.0) break;
    }
    close(fd);
    const double secs = mg_now() - t0;
    std::printf("ps5mg: bench-read %s block %zu: %.0f MB in %.2f s = %.0f MB/s\n", path.c_str(), block, total / 1e6, secs,
                total / 1e6 / std::max(secs, 1e-9));
    return 0;
}

// "bench-fread <file> <block bytes> <stdio buffer MiB>": like bench-read, but through stdio (what the weights loading
// uses), with a buffer of that size; first read of the file, at most 8 seconds.
int bench_fread(const std::string & line) {
    std::istringstream words(line);
    std::string        cmd, path;
    size_t             block = 1 << 20, bufmib = 0;
    words >> cmd >> path >> block >> bufmib;
    FILE * f = fopen(path.c_str(), "rb");
    if (!f) {
        std::printf("ps5mg: bench-fread: cannot open %s\n", path.c_str());
        return 2;
    }
    if (bufmib > 0) setvbuf(f, nullptr, _IOFBF, bufmib << 20);
    std::vector<char> buf(block);
    size_t            total = 0, n;
    const double      t0 = mg_now();
    while ((n = fread(buf.data(), 1, block, f)) > 0) {
        total += n;
        if (mg_now() - t0 > 8.0) break;
    }
    fclose(f);
    const double secs = mg_now() - t0;
    std::printf("ps5mg: bench-fread %s block %zu stdio buffer %zu MiB: %.0f MB in %.2f s = %.0f MB/s\n", path.c_str(), block, bufmib,
                total / 1e6, secs, total / 1e6 / std::max(secs, 1e-9));
    return 0;
}

// "bench-malloc <MiB>": how fast fresh memory is handed out and touched (page faults), three rounds; and a single
// thread's integer loop, so a slow CPU and slow memory can be told apart.
int bench_malloc(const std::string & line) {
    std::istringstream words(line);
    std::string        cmd;
    size_t             mib = 512;
    words >> cmd >> mib;
    for (int round = 0; round < 3; round++) {
        const double t0 = mg_now();
        char *       p  = static_cast<char *>(malloc(mib << 20));
        if (!p) {
            std::printf("ps5mg: bench-malloc: cannot allocate %zu MiB\n", mib);
            return 2;
        }
        const double t1 = mg_now();
        for (size_t i = 0; i < (mib << 20); i += 4096) reinterpret_cast<volatile char *>(p)[i] = static_cast<char>(i);  // one touch per page
        const double t2 = mg_now();
        free(p);
        std::printf("ps5mg: bench-malloc %zu MiB: malloc %.3f s, first touch %.3f s = %.0f MB/s\n", mib, t1 - t0, t2 - t1,
                    (mib << 20) / 1e6 / std::max(t2 - t1, 1e-9));
    }
    volatile uint64_t x = 1;
    const double t0 = mg_now();
    for (uint64_t i = 0; i < 400000000ull; i++) x = x * 6364136223846793005ull + 1442695040888963407ull;
    std::printf("ps5mg: bench-malloc cpu loop (4e8 steps): %.2f s\n", mg_now() - t0);
    return 0;
}

// "bench-upload <MiB> <chunk MiB>": host memory to a Vulkan buffer through ggml_backend_tensor_set, against a plain
// memcpy to host memory (what the weights loading does per tensor).
int bench_upload(const std::string & line) {
    std::istringstream words(line);
    std::string        cmd;
    size_t             mib = 256, chunk_mib = 16;
    words >> cmd >> mib >> chunk_mib;
    const size_t bytes = mib << 20, chunk = chunk_mib << 20;
    ggml_backend_t backend = nullptr;
    for (size_t i = 0; i < ggml_backend_dev_count() && !backend; i++) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(dev) != GGML_BACKEND_DEVICE_TYPE_CPU) backend = ggml_backend_dev_init(dev, nullptr);  // an iGPU, here
    }
    if (!backend) {
        std::printf("ps5mg: bench-upload: no GPU backend\n");
        return 2;
    }
    ggml_init_params ip = {ggml_tensor_overhead() * 4, nullptr, true};
    ggml_context *   ctx = ggml_init(ip);
    ggml_tensor *    t   = ggml_new_tensor_1d(ctx, GGML_TYPE_I8, static_cast<int64_t>(bytes));
    const double ta = mg_now();
    ggml_backend_buffer_t buf = ggml_backend_alloc_ctx_tensors(ctx, backend);
    if (!buf) {
        std::printf("ps5mg: bench-upload: cannot allocate %zu MiB\n", mib);
        return 2;
    }
    std::printf("ps5mg: bench-upload: allocating %zu MiB took %.2f s\n", mib, mg_now() - ta);
    // The first write of fresh pages and the second differ: the first time the kernel hands out the memory.
    std::vector<char> host(chunk, 1), host2(chunk);
    double t0 = mg_now();
    for (int pass = 0; pass < 2; pass++) {
        const double tp = mg_now();
        for (size_t off = 0; off < bytes; off += chunk) ggml_backend_tensor_set(t, host.data(), off, std::min(chunk, bytes - off));
        std::printf("ps5mg: bench-upload pass %d: %.2f s = %.0f MB/s\n", pass, mg_now() - tp, bytes / 1e6 / std::max(mg_now() - tp, 1e-9));
    }
    t0 = mg_now();
    for (size_t off = 0; off < bytes; off += chunk) ggml_backend_tensor_set(t, host.data(), off, std::min(chunk, bytes - off));
    const double up = mg_now() - t0;
    t0 = mg_now();
    for (size_t off = 0; off < bytes; off += chunk) memcpy(host2.data(), host.data(), std::min(chunk, bytes - off));
    const double cp = mg_now() - t0;
    std::printf("ps5mg: bench-upload %zu MiB in %zu MiB chunks: tensor_set %.2f s = %.0f MB/s; host memcpy %.2f s = %.0f MB/s\n", mib,
                chunk_mib, up, bytes / 1e6 / std::max(up, 1e-9), cp, bytes / 1e6 / std::max(cp, 1e-9));
    ggml_backend_buffer_free(buf);
    ggml_free(ctx);
    ggml_backend_free(backend);
    return 0;
}

// One job: the trellis-cli arguments, one line in a .job file.
int run_job(const std::string & name, const std::string & line) {
    if (line.rfind("bench-fread ", 0) == 0) {
        std::printf("ps5mg: JOB %s START: %s\n", name.c_str(), line.c_str());
        const int rc = bench_fread(line);
        std::printf("ps5mg: JOB %s DONE rc=%d\n", name.c_str(), rc);
        std::ofstream(std::string(kJobs) + "/" + name + ".result") << rc << " 0\n";
        return rc;
    }
    if (line.rfind("bench-malloc ", 0) == 0) {
        std::printf("ps5mg: JOB %s START: %s\n", name.c_str(), line.c_str());
        const int rc = bench_malloc(line);
        std::printf("ps5mg: JOB %s DONE rc=%d\n", name.c_str(), rc);
        std::ofstream(std::string(kJobs) + "/" + name + ".result") << rc << " 0\n";
        return rc;
    }
    if (line.rfind("bench-upload ", 0) == 0) {
        std::printf("ps5mg: JOB %s START: %s\n", name.c_str(), line.c_str());
        const int rc = bench_upload(line);
        std::printf("ps5mg: JOB %s DONE rc=%d\n", name.c_str(), rc);
        std::ofstream(std::string(kJobs) + "/" + name + ".result") << rc << " 0\n";
        return rc;
    }
    if (line.rfind("bench-read ", 0) == 0) {
        std::printf("ps5mg: JOB %s START: %s\n", name.c_str(), line.c_str());
        const int rc = bench_read(line);
        std::printf("ps5mg: JOB %s DONE rc=%d\n", name.c_str(), rc);
        std::ofstream(std::string(kJobs) + "/" + name + ".result") << rc << " 0\n";
        return rc;
    }
    std::istringstream words(line);
    std::vector<std::string> args{"trellis-cli"};
    for (std::string w; words >> w;) args.push_back(w);
    std::vector<char *> argv;
    for (auto & a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::printf("ps5mg: JOB %s START: %s\n", name.c_str(), line.c_str());
    // Pixal3D's mesh is y-up already (the GLB writer is told, see scripts/build-mg.sh).
    if (line.find("--model pixal3d") != std::string::npos) setenv("PS5MG_YUP_NATIVE", "1", 1);
    else unsetenv("PS5MG_YUP_NATIVE");
    log_memory("before");
    {
        std::lock_guard<std::mutex> lk(g_mg.mu);
        g_mg.job    = name;
        g_mg.stage  = "starting";
        g_mg.job_t0 = mg_now();
    }
    const auto t0 = std::chrono::steady_clock::now();
    const int  rc = args.size() > 1 ? trellis_cli_main(static_cast<int>(args.size()), argv.data()) : 2;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    log_memory("after");
    std::printf("ps5mg: JOB %s DONE rc=%d in %.1f s\n", name.c_str(), rc, secs);
    {
        std::lock_guard<std::mutex> lk(g_mg.mu);
        g_mg.job.clear();
        g_mg.stage = "idle";
        g_mg.progress.clear();
    }
    std::ofstream(std::string(kJobs) + "/" + name + ".result") << rc << ' ' << secs << '\n';
    return rc;
}

// The first *.job in /data/PS5MG/jobs by name, or "".
std::string next_job() {
    std::vector<std::string> names;
    if (DIR * d = opendir(kJobs)) {
        while (dirent * e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".job") == 0) names.push_back(n);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    return names.empty() ? "" : names.front();
}

// Jobs one after the other, until the quit file or until the pool is too low for another (freed memory does not
// come back to the pool on the console; the title then ends itself and the next start has all of it).
void * run(void *) {
    std::printf("ps5mg: ps5mg_threads=%u hardware_concurrency=%u\n", ps5mg_threads(), std::thread::hardware_concurrency());
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
        std::printf("ps5mg: device %zu: %s\n", i, ggml_backend_dev_name(ggml_backend_dev_get(i)));
    }
    mkdir(kJobs, 0777);
    mkdir("/data/PS5MG/out", 0777);
    // A job still marked running was in the title when it went away (a crash, the console): failed.
    if (DIR * d = opendir(kJobs)) {
        std::vector<std::string> left;
        while (dirent * e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 8 && n.compare(n.size() - 8, 8, ".running") == 0) left.push_back(n.substr(0, n.size() - 8));
        }
        closedir(d);
        for (const auto & id : left) {
            std::printf("ps5mg: job %s was running when the title ended: marked failed\n", id.c_str());
            rename((std::string(kJobs) + "/" + id + ".running").c_str(), (std::string(kJobs) + "/" + id + ".failed").c_str());
        }
    }
    struct stat st;
    while (stat(kQuit, &st) != 0) {
        const std::string job = next_job();
        if (job.empty()) {
            sleep(2);
            continue;
        }
        const std::string path = std::string(kJobs) + "/" + job;
        // An FTP upload creates the file before it fills it: an empty one is read again a few times, then left.
        std::string line;
        for (int tries = 0; tries < 8 && line.empty(); tries++) {
            if (tries > 0) usleep(500000);
            std::ifstream in(path);
            std::getline(in, line);
        }
        if (line.empty()) {
            sleep(2);
            continue;
        }
        // Claim it first: a job that crashes the title is not run again by the next start.
        const std::string name = job.substr(0, job.size() - 4), base = std::string(kJobs) + "/" + name;
        rename(path.c_str(), (base + ".running").c_str());
        const int rc = run_job(name, line);
        rename((base + ".running").c_str(), (base + (rc == 0 ? ".done" : ".failed")).c_str());
        // A second job in the same process runs 4 to 9 times slower (the GPU stages too), so the title starts
        // over after every job; the queue is on disk and the next start picks it up.
        g_restart = true;
        break;
    }
    std::printf("ps5mg: worker ends\n");
    std::fflush(nullptr);
    g_done = true;
    return nullptr;
}
}  // namespace

int main() {
    sceSystemServiceHideSplashScreen();
    mkdir(kDir, 0777);
    mkdir(kLogs, 0777);
    // Both in append mode: with "w" for one of them the two write at different offsets and garble each other's lines.
    { FILE * f = fopen(kRaw, "w"); if (f) fclose(f); }
    freopen(kRaw, "a", stdout);
    freopen(kRaw, "a", stderr);
    pthread_t lt;
    pthread_create(&lt, nullptr, stamp_log, nullptr);
    // Whole lines to the file: unbuffered, the console's libc writes a character at a time and the two streams
    // garble each other. stdout is flushed by the log thread, so the progress bars (which end in a carriage return) still show.
    setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
    setvbuf(stderr, nullptr, _IOLBF, 1 << 12);
    setenv("radv_enable_unified_heap_on_apu", "true", 1);
    setenv("TRELLIS_DBG_DEFORM", "1", 1);  // per-call timing of the deform conv in the log
    std::printf("ps5mg: start\n");
#ifdef MG_SCREEN
    // The screen first: it takes from the same memory pool as the models.
    const bool screen = mg_screen_open();
#else
    const bool screen = false;
#endif

    // 16 MiB stack: the pipeline recurses deeper than the main thread's default allows.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_t th;
    if (pthread_create(&th, &attr, run, nullptr) != 0) {
        std::printf("ps5mg: cannot start the worker\n");
        g_done = true;
    }

    // The web interface, in a thread of its own; it ends with the title.
    pthread_attr_t wattr;
    pthread_attr_init(&wattr);
    pthread_attr_setstacksize(&wattr, 4u << 20);
    pthread_t web;
    pthread_create(&web, &wattr, [](void *) -> void * { mg_web_run(7870); return nullptr; }, nullptr);

    // The screen (or, without one, a pause) until the title ends itself, which it does when asked (the quit file) or
    // after a job, and never in the middle of one.
    double next_pool = 0;
    while (!g_done) {
        if (mg_now() >= next_pool) {
            next_pool = mg_now() + 1;
            int64_t       at   = 0;
            size_t        free = 0;
            const int64_t pool = sceKernelGetDirectMemorySize();
            sceKernelAvailableDirectMemorySize(0, pool, 2 * 1024 * 1024, &at, &free);
            std::lock_guard<std::mutex> lk(g_mg.mu);
            g_mg.pool_free_gib = free / kGiB;
            g_mg.pool_gib      = pool / kGiB;
            g_mg.threads       = ps5mg_threads();
        }
#ifdef MG_SCREEN
        if (screen) {
            mg_screen_frame();
            continue;
        }
#endif
        usleep(200000);
    }
    if (g_restart) {
        std::printf("ps5mg: restarting for the next job\n");
        std::fflush(nullptr);
        usleep(1500000);  // the log thread copies the last lines
        const char * none[] = {nullptr};
        const int    r      = sceSystemServiceLoadExec("/app0/eboot.bin", none);
        std::printf("ps5mg: LoadExec of /app0/eboot.bin returned 0x%08x\n", static_cast<unsigned>(r));
        std::fflush(nullptr);
        usleep(1500000);
    }
    unlink(kQuit);
    std::fflush(nullptr);
    sceSystemServiceLoadExec("exit", nullptr);
    return 0;
}
