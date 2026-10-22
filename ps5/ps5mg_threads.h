// trellis.cpp sizes its worker pools with std::thread::hardware_concurrency(), which says 16 on the console while a
// process may only run on the CPUs in its affinity mask, shared with the console's services (PS5SD's
// patches/sdcpp/0002, PS5LM's patches/0001). scripts/build-mg.sh replaces the calls with this, and trellis_core
// is compiled with -include of this file. Mask popcount minus two, at least one.
#pragma once
#include <pthread.h>

#include <algorithm>
#include <cstdint>

#include <thread>

#if defined(__PROSPERO__)
extern "C" int scePthreadGetaffinity(pthread_t thread, uint64_t * mask);
#endif

inline unsigned ps5mg_threads() {
#if defined(__PROSPERO__)
    uint64_t mask = 0;
    if (scePthreadGetaffinity(pthread_self(), &mask) == 0 && mask != 0) {
        return static_cast<unsigned>(std::max(1, __builtin_popcountll(mask) - 2));
    }
    return 2;
#else
    return std::max(1u, std::thread::hardware_concurrency());  // off the console: all of them
#endif
}

// pread() the whole range: the console's stdio fread() runs at 92 MB/s, whatever the block or buffer size, where
// read() does 1 to 2 GB/s (PS5MG's bench-fread and bench-read jobs). scripts/build-mg.sh has the weight loading use this.
#include <unistd.h>

inline bool mg_pread_full(int fd, void * dst, size_t n, int64_t off) {
    char * p = static_cast<char *>(dst);
    while (n > 0) {
        const ssize_t r = pread(fd, p, n, static_cast<off_t>(off));
        if (r <= 0) return false;
        p += r;
        off += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}
