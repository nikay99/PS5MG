// PS5MG's screen on the PC: renders a few states of the dashboard to PNG files through Mesa's surfaceless EGL
// (llvmpipe), with the same calls mg_screen.cpp makes (AppStatus setters, Dashboard::update, Dashboard::compose).
// Adapted from PS5SD's ps5/app/preview/preview_main.cpp (GPL-3.0). Built and run by tools/preview-screen.sh.
//
//   preview_mg <font dir> <output dir> <sample dir> [width height]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app_status.hpp"
#include "dashboard.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "image_file.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <sys/stat.h>

namespace {

bool open_context() {
    auto get_platform_display =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = get_platform_display != nullptr
                             ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                             : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API)) return false;
    const EGLint attributes[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                                  EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    return context != EGL_NO_CONTEXT && eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

std::vector<uint8_t> rgb_of(const ps5sd::Rgba & img) {
    std::vector<uint8_t> rgb((size_t) img.width * img.height * 3);
    for (size_t i = 0; i < (size_t) img.width * img.height; i++)
        for (int c = 0; c < 3; c++) rgb[i * 3 + c] = img.pixels[i * 4 + c];
    return rgb;
}

}  // namespace

int main(int argc, char ** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <font dir> <output dir> <sample dir> [width height]\n", argv[0]);
        return 2;
    }
    const std::string fonts = argv[1], output = argv[2], samples = argv[3];
    const int         width = argc > 5 ? std::atoi(argv[4]) : 1920, height = argc > 5 ? std::atoi(argv[5]) : 1080;
    mkdir(output.c_str(), 0777);
    if (!open_context()) {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    hui::gfx::set_glsl_prefix("#version 450 core\n");
    hui::gfx::Renderer renderer;
    ps5sd::Dashboard   dashboard;
    if (!renderer.init() || !dashboard.load(renderer, fonts + "/dejavu-sans-mono.huifont")) {
        std::fprintf(stderr, "the dashboard did not load\n");
        return 1;
    }
    GLuint framebuffer = 0, color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 1;

    ps5sd::AppStatus           status;
    std::vector<unsigned char> pixels((size_t) width * height * 4);
    const auto                 frames = [&](int n) {
        for (int i = 0; i < n; i++) dashboard.update(status.snapshot(), 1.0f / 60.0f);
    };
    const auto capture = [&](const std::string & name, double job_s, double last_job_s) {
        ps5sd::AppStatus::Snapshot s = status.snapshot();
        s.job_s                      = job_s;
        s.last_job_s                 = last_job_s >= 0 ? last_job_s : s.last_job_s;
        dashboard.update(s, 1.0f / 60.0f);
        dashboard.compose(renderer, "0.2.2");
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.present(framebuffer, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        const std::string path = output + "/" + name + ".png";
        stbi_flip_vertically_on_write(1);
        const bool ok = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
        std::fprintf(stderr, "wrote %s: %zu shapes, GL error 0x%x\n", path.c_str(), renderer.last_instances(), glGetError());
        return ok;
    };

    const std::string pic[] = { samples + "/z_lantern.png", samples + "/sword.png", samples + "/z_shield.png",
                                samples + "/z_potion.png", samples + "/z_blaster.png" };
    ps5sd::Rgba       lantern;
    if (!ps5sd::load_image_rgba(pic[0], 512, lantern)) {
        std::fprintf(stderr, "no sample picture in %s\n", samples.c_str());
        return 1;
    }
    bool ok = true;

    // 1. Ready, the address known.
    status.set_port(7870);
    status.set_model("TRELLIS.2 (q4)");
    status.set_memory(10.9, 11.44);
    status.set_address("192.168.0.10");
    status.set_phase("Ready");
    frames(120);
    ok = capture("01-ready", 0, -1) && ok;

    // 2. A job: the picture, the stage, 45 percent.
    status.set_phase("Generating", "oil lantern  -  shape SLAT flow");
    const auto rgb = rgb_of(lantern);
    status.set_preview(rgb.data(), lantern.width, lantern.height);
    status.set_progress(450, 1000, 0.1f);
    status.set_memory(8.4, 11.44);
    status.log("[1/6] preprocess (BiRefNet bg removal, 512)");
    status.log("[2/6] DINOv3 conditioning");
    status.log("[3/6] sparse-structure flow + decode");
    status.log("      [flow] 8 steps, 15 forwards, 42.9s");
    status.log("[4/7] shape SLAT flow (512)");
    frames(90);
    ok = capture("02-generating", 61.0, -1) && ok;

    // 3. Done: the pictures of five meshes.
    status.set_phase("Ready");
    for (int i = 0; i < 5; i++) {
        status.set_result(pic[(size_t) i]);
        frames(4);
    }
    status.log("done in 113.3s -> /data/PS5MG/out/c0_lantern_tex.glb");
    frames(240);
    ok = capture("03-done", 0, 113.3) && ok;

    // 4. An error.
    status.set_phase("Job failed", "Out of device memory in the texture stage. See /data/PS5MG/app.log");
    frames(60);
    ok = capture("04-error", 0, -1) && ok;

    // 5. The log.
    status.set_phase("Ready");
    dashboard.toggle_log();
    frames(30);
    ok = capture("05-log", 0, 113.3) && ok;
    return ok ? 0 : 1;
}
