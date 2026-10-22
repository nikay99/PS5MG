// Host test for ps5/mg_web.cpp: the real web server code with a stub state, run in the build container (tools/test-web/run.sh).
#include <chrono>
#include <thread>

#include "mg_state.hpp"

MgState g_mg;
double  mg_now() { return 1.0; }

int main() {
    mg_web_run(7870);
    return 0;
}
