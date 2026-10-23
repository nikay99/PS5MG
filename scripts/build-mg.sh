#!/usr/bin/env bash
# Builds PS5MG (trellis.cpp as a PS5 title) in the ps5sd-build container (docker/ of the PS5SD submodule),
# with the repository mounted at /work, after scripts/setup-deps.sh (see docs/BUILDING.md). Recipe: PS5SD's
# scripts/build-app.sh (adapted from PS5LM, GPL-3.0).
#   .deps/trellis.cpp       trellis.cpp @ 8e4ce11 + pwilkin/ggml @ f8697b3 + PS5SD's patches/ggml (cloned here)
#   ps5/                    this repository's title (CMakeLists.txt, mg_main.cpp, mg_web.cpp, screen/); sce_sys/ at the root
#   third_party/PS5SD       the PS5SD submodule: toolchain wrappers, deps (.deps), the screen's UI kit
#
# The screen (PS5SD's dashboard through ps5-opengl and ps5-homebrew-ui) is built when the ps5-opengl SDK is in
# third_party/PS5SD/.deps/ps5-opengl-sdk (setup-deps.sh makes it); MG_DISPLAY=0 builds without it.
set -euo pipefail
W=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
P=$W/third_party/PS5SD                         # PS5SD: toolchain, wrappers, deps
vk=$P/.deps/src/PS5_Vulkan
sdk=$vk/.deps/native/ps5-payload-sdk
archive=$vk/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a
tool=$vk/build/host/ps5-native-tool
app_src=$P/ps5/app
kit=$P/third_party/ps5-homebrew-ui
glsdk=$P/.deps/ps5-opengl-sdk
out=$W/build
export PS5_PAYLOAD_SDK=$sdk PS5_VULKAN_ROOT=$vk
mkdir -p "$out/obj" "$out/stubs" "$W/.deps"

display=1
if [ "${MG_DISPLAY:-1}" = 0 ] || [ ! -f "$glsdk/lib/libPS5OpenGL.a" ] || [ ! -d "$kit/src" ]; then display=0; fi
echo "build-mg: screen: $([ $display = 1 ] && echo "ps5-opengl at $glsdk" || echo none)"

# sed -i that leaves a file alone when the edit changes nothing (the build system sees no new timestamp, so nothing
# is compiled again): sedi <expression> <file>...
sedi() {
    local expr=$1 f t
    shift
    for f in "$@"; do
        t=$(mktemp)
        sed "$expr" "$f" > "$t"
        if cmp -s "$t" "$f"; then rm -f "$t"; else cat "$t" > "$f"; rm -f "$t"; fi
    done
}
T=$W/.deps/trellis.cpp
if [ ! -d "$T/.git" ]; then
    git clone -q https://github.com/pwilkin/trellis.cpp "$T"
    git -C "$T" checkout -q 8e4ce11a77d35c3e3a4b3f84a29d92bf5491e64e
fi
# Worker pools by the CPUs the title may use, not by hardware_concurrency() (ps5/ps5mg_threads.h).
sedi 's/std::thread::hardware_concurrency()/ps5mg_threads()/g' "$T"/src/*.cpp
# The PLY next to every GLB doubles the file writes (14 s on the console): off unless wanted.
sedi '/TRELLIS_KEEP_PLY/!s/^    trellis::write_ply(ply.c_str()/    if (!std::getenv("TRELLIS_KEEP_PLY")) {} else trellis::write_ply(ply.c_str()/' "$T/src/trellis_cli.cpp"
# Pixal3D meshes come out of the decoder y-up already (TRELLIS ones z-up); write_glb turns every mesh from z-up to
# y-up, which left Pixal3D's lying on their back. mg_main.cpp sets PS5MG_YUP_NATIVE for a --model pixal3d job.
sedi '/PS5MG_YUP_NATIVE/!{s/float p\[3\] = { ox, oz, -oy };/float p[3] = { ox, oz, -oy }; if (std::getenv("PS5MG_YUP_NATIVE")) { p[1] = oy; p[2] = oz; }/; s/float p\[3\]={ox,oz,-oy};/float p[3]={ox,oz,-oy}; if (std::getenv("PS5MG_YUP_NATIVE")) { p[1] = oy; p[2] = oz; }/}' "$T/src/mesh_glb.cpp"
# BiRefNet's bilinear upscale: parallel, channel by channel (ps5/birefnet_interp.inc).
python3 "$W/ps5/patch-trellis-io.py" "$T/src/trellis_model.cpp"
python3 "$W/ps5/patch-notex-faces.py" "$T/src/trellis_cli.cpp"
python3 "$W/ps5/patch-birefnet.py" "$T/src/birefnet.cpp" "$W/ps5/birefnet_interp.inc"
# Big stdio buffers for the weights and the GLB: the console's libc reads and writes a file in buffer-sized pieces
# (a few KB by default), which held the weight loading to about 90 MB/s.
sedi '/setvbuf/!s|^    FILE\* f = fopen(path.c_str(), "rb");|    FILE* f = fopen(path.c_str(), "rb"); if (f) setvbuf(f, nullptr, _IOFBF, 16 << 20);|' "$T/src/trellis_model.cpp"
sedi '/setvbuf/!{s|^    FILE\* f = fopen(path, "wb");|    FILE* f = fopen(path, "wb"); if (f) setvbuf(f, nullptr, _IOFBF, 8 << 20);|; s|FILE\* f=fopen(path,"wb"); if(!f) return false;|FILE* f=fopen(path,"wb"); if(!f) return false; setvbuf(f, nullptr, _IOFBF, 8 << 20);|}' "$T/src/mesh_glb.cpp"
G=$T/thirdparty/ggml
if [ ! -f "$G/CMakeLists.txt" ]; then
    rm -rf "$G"
    git clone -q https://github.com/pwilkin/ggml "$G"
    git -C "$G" checkout -q f8697b33795db2ee6a2569b40853ca6aaa69f9b9
    for p in "$P"/patches/ggml/*.patch; do git -C "$G" apply "$p"; echo "applied $(basename "$p")"; done
fi

# Vulkan headers and the clang shim: as in build-app.sh.
headers=$P/.deps/ps5-vk-headers
if [ ! -f "$headers/include/vulkan/vulkan.hpp" ]; then
    mkdir -p "$headers/include" "$headers/cmake"
    cp -r /usr/include/vulkan /usr/include/vk_video /usr/include/spirv "$headers/include/"
    cat > "$headers/cmake/SPIRV-HeadersConfig.cmake" <<EOT
add_library(SPIRV-Headers::SPIRV-Headers INTERFACE IMPORTED)
set_target_properties(SPIRV-Headers::SPIRV-Headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "$headers/include")
set(SPIRV-Headers_FOUND TRUE)
EOT
fi
shim=$P/.deps/clang-shim
builtins=$(clang --print-file-name=libclang_rt.builtins.a)
[ -f "$builtins" ] || builtins="$(clang --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
mkdir -p "$shim/resource/lib/linux"
ln -sf "$builtins" "$shim/resource/lib/linux/libclang_rt.builtins-x86_64.a"
printf '#!/bin/sh\n[ "$1" = --print-resource-dir ] && { echo "%s"; exit 0; }\nexec clang "$@"\n' "$shim/resource" > "$shim/clang"
chmod +x "$shim/clang"

flags="-I$headers/include"
cxxflags="$flags -fexceptions -fcxx-exceptions -frtti"
cmake -S "$W/ps5" -B "$out/cm" -G Ninja \
    -DTRELLIS_SRC="$T" -DCMAKE_TOOLCHAIN_FILE="$app_src/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$cxxflags" \
    -DBUILD_SHARED_LIBS=OFF \
    -DGGML_VULKAN=ON -DGGML_BACKEND_DL=OFF -DGGML_NATIVE=OFF -DGGML_CPU_ALL_VARIANTS=OFF \
    -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON -DGGML_AVX512=OFF \
    -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF \
    -DVulkan_INCLUDE_DIR="$headers/include" -DVulkan_LIBRARY="$archive" \
    -DVulkan_GLSLC_EXECUTABLE="$(command -v glslc)" -DSPIRV-Headers_DIR="$headers/cmake" \
    -DMATH_LIBRARY=SceLibcInternal > "$out/configure.log" 2>&1 || { tail -30 "$out/configure.log" >&2; exit 1; }
cmake --build "$out/cm" --target trellis_core trellis_cli_main

# The title's own objects.
obj=$out/obj
cxx() { "$app_src/ps5cxx" -std=c++20 -O2 -ffunction-sections -fdata-sections "$@"; }
eh=(-fexceptions -fcxx-exceptions -frtti)
if [ $display = 1 ]; then
    kitinc=(-I "$kit/src" -I "$kit/third_party" -I "$glsdk/include" -DGL_GLEXT_PROTOTYPES=1)
    screen_def=(-DMG_SCREEN=1)
else
    kitinc=()
    screen_def=()
fi
cxx "${eh[@]}" "${screen_def[@]}" -I "$headers/include" -I "$G/include" -I "$W/ps5" -c "$W/ps5/mg_main.cpp" -o "$obj/mg_main.o"
sh "$P/scripts/embed-file.sh" "$W/ps5/index.html" mg_index_html "$out/mg_index_html.h"
cxx -std=c++17 "${eh[@]}" -I "$G/include" -I "$W/ps5" -I "$out" -I "$T/thirdparty/cpp-httplib" -c "$W/ps5/mg_web.cpp" -o "$obj/mg_web.o"
"$app_src/ps5cc" -O2 -c "$app_src/compat_app.c" -o "$obj/compat_app.o"
rm -f "$obj/libps5app_compat.a"; "$sdk/bin/llvm-ar" rcs "$obj/libps5app_compat.a" "$obj/compat_app.o"
cxx -fno-exceptions -fno-rtti -c "$vk/tooling/native/app_crt.cpp" -o "$obj/app_crt.o"
cxx -fexceptions -fcxx-exceptions -c "$app_src/cpp_runtime.cpp" -o "$obj/app_cpp_runtime.o"
app_objects=("$obj/app_crt.o" "$obj/app_cpp_runtime.o" "$obj/mg_main.o" "$obj/mg_web.o")

# ---- the screen (display builds only): as in build-app.sh ----------------------------------------
gl=
if [ $display = 1 ]; then
    scr=$W/ps5/screen
    for src in display dashboard qrcodegen app_status glb_read mesh_view; do
        cxx "${eh[@]}" "${kitinc[@]}" -I "$scr" -c "$scr/$src.cpp" -o "$obj/$src.o"
    done
    cxx "${eh[@]}" -I "$T/thirdparty/stb" -I "$scr" -c "$scr/image_file.cpp" -o "$obj/image_file.o"
    cxx "${eh[@]}" "${kitinc[@]}" -I "$scr" -I "$W/ps5" -c "$W/ps5/mg_screen.cpp" -o "$obj/mg_screen.o"
    app_objects+=("$obj/display.o" "$obj/dashboard.o" "$obj/qrcodegen.o" "$obj/app_status.o" "$obj/glb_read.o" "$obj/mesh_view.o" "$obj/image_file.o" "$obj/mg_screen.o")

    # ps5-opengl is a Mesa of its own and shares thousands of symbol names with RADV's: linked into one object
    # with only the GL and EGL API left global, the two never meet.
    gl="$out/gl/gl_api.o"
    if [ ! -f "$gl" ] || [ "$glsdk/lib/libPS5OpenGL.a" -nt "$gl" ]; then
        mkdir -p "$out/gl"
        gllibs=()
        for l in $(sed -n '/GROUP (/,/)/p' "$glsdk/lib/libPS5OpenGL.a" | grep -oE 'lib[a-z0-9_.]+\.a'); do
            gllibs+=("$glsdk/lib/$l")
        done
        "$sdk/bin/llvm-nm" --defined-only -g "$glsdk/lib/libps5_opengl_core33.a" "$glsdk/lib/libglapi_bridge.a" \
            "$glsdk/lib/libglapi.a" 2>/dev/null | awk 'NF>=3 && $2 ~ /[TW]/ {print $3}' |
            grep -E '^(gl|egl)[A-Z]|^ps5_opengl' | sort -u > "$out/gl/api.txt"
        us=()
        while read -r name; do us+=(-u "$name"); done < "$out/gl/api.txt"
        "$sdk/bin/ld.lld" -r "${us[@]}" -u ps5_agc_gate2_run --start-group "${gllibs[@]}" --end-group -o "$out/gl/gl_all.o"
        "$sdk/bin/llvm-objcopy" --keep-global-symbols="$out/gl/api.txt" "$out/gl/gl_all.o" "$gl"
        rm -f "$out/gl/gl_all.o" "$out/gl/libps5mg_gl.a"
        "$sdk/bin/llvm-ar" rcs "$out/gl/libps5mg_gl.a" "$gl"
    fi
    # ps5-homebrew-ui's kit (gfx, ui, core, audio and its console platform layer), drawn through ps5-opengl.
    kitflags=(-O2 -ffunction-sections -fdata-sections "${kitinc[@]}" -I "$scr")
    mkdir -p "$obj/kit"
    mapfile -t kit_sources < <(find "$kit/src/gfx" "$kit/src/ui" "$kit/src/core" "$kit/src/audio" "$kit/src/third_party" \
        "$kit/src/platform/ps5" -type f \( -name '*.cpp' -o -name '*.c' \) ! -name display_egl.cpp | sort)
    kit_objects=()
    for src in "${kit_sources[@]}"; do
        o="$obj/kit/$(echo "${src#"$kit/src/"}" | tr '/' '_').o"
        kit_objects+=("$o")
        [ -f "$o" ] && [ "$o" -nt "$src" ] && continue
        if [[ $src == *.c ]]; then
            "$app_src/ps5cc" -std=c11 -O2 -w -I "$kit/src" -c "$src" -o "$o" &
        else
            cxx "${kitflags[@]}" -w -c "$src" -o "$o" &
        fi
        (( $(jobs -r | wc -l) >= $(nproc) )) && wait -n
    done
    wait
    rm -f "$obj/libps5mg_kit.a"
    "$sdk/bin/llvm-ar" rcs "$obj/libps5mg_kit.a" "${kit_objects[@]}"
fi

# AGC stubs: the imports every driver of the title makes (the GL object and RADV; RADV alone without a screen).
agc_imports=$("$sdk/bin/llvm-nm" -u $gl "$archive" 2>/dev/null | awk '{print $NF}' | grep -E '^sceAgc' | sort -u)
for lib in libSceAgc libSceAgcDriver; do
    stub_c=$out/stubs/$lib.c; : > "$stub_c"
    for name in $agc_imports; do
        case $lib:$name in
            libSceAgcDriver:sceAgcDriver*) echo "void $name(void) {}" >> "$stub_c" ;;
            libSceAgc:sceAgcDriver*) ;;
            libSceAgc:*) echo "void $name(void) {}" >> "$stub_c" ;;
        esac
    done
    "$app_src/ps5cc" -O2 -fPIC -c "$stub_c" -o "$obj/${lib}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "$lib.prx" -o "$out/stubs/$lib.so" "$obj/${lib}_stub.o"
done
stubs=("$out/stubs/libSceAgc.so" "$out/stubs/libSceAgcDriver.so")

# shellcheck source=/dev/null
source "$vk/tools/radv-link.sh"
PS5_CLANG="$shim/clang" radv_link_recipe "$vk" "$sdk" "$archive" || exit 2
link_flags=()
for f in "${radv_link_flags[@]}"; do
    case $f in --defsym=getaddrinfo=*|--defsym=freeaddrinfo=*) ;; *) link_flags+=("$f") ;; esac
done
# Both drivers initialise AGC; display.cpp's wrap lets the first call do it.
[ $display = 1 ] && link_flags+=(--wrap=sceAgcInit)
mapfile -t libs < <(find "$out/cm" -name '*.a' | sort)
local_archives=("${libs[@]}" "$obj/libps5app_compat.a")
[ $display = 1 ] && local_archives+=("$obj/libps5mg_kit.a" "$out/gl/libps5mg_gl.a")
sdk_stubs=("$sdk"/target/lib/*.so)

"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${link_flags[@]}" \
    --version-script "$app_src/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$out/llvm-pie.elf" "${app_objects[@]}" \
    --start-group "${local_archives[@]}" --end-group "${stubs[@]}" "${radv_link_inputs[@]}" \
    --as-needed "${sdk_stubs[@]}" 2>&1 | tee "$out/link.log" | head -60
[ -f "$out/llvm-pie.elf" ] || exit 1

"$tool" link --in "$out/llvm-pie.elf" --out "$out/eboot.elf" --stub-dir "$sdk/target/lib" \
    --stub "$out/stubs/libSceAgc.so" --stub "$out/stubs/libSceAgcDriver.so" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf

app=$out/title/PPSA99577
rm -rf "$app"; mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$out/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$W/sce_sys/param.json" "$app/sce_sys/param.json"
# Icon and backgrounds are PS5SD's (placeholders; ShadowMountPlus copies them at registration).
cp "$W"/sce_sys/{icon0.png,icon0.dds,pic0.dds,pic1.dds} "$app/sce_sys/"
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
if [ $display = 1 ]; then
    # The dashboard's fonts, with their licences (main reads /app0/assets/fonts/dejavu-sans-mono.huifont).
    mkdir -p "$app/assets/fonts"
    cp "$kit/assets/fonts/"{inter-regular,inter-semibold,montserrat-medium,dejavu-sans-mono}.huifont \
        "$kit/assets/fonts/"{Inter,Montserrat,DejaVu}-LICENSE.txt "$app/assets/fonts/"
fi
# Licences and the source offer (GPL-3.0 title; trellis.cpp and ggml are MIT).
mkdir -p "$app/licenses"
cp "$W/LICENSE" "$app/licenses/PS5MG-GPL-3.0.txt"
cp "$T/LICENSE" "$app/licenses/trellis.cpp-MIT.txt"
cp "$T/thirdparty/ggml/LICENSE" "$app/licenses/ggml-MIT.txt"
cp "$W/THIRD_PARTY_NOTICES.md" "$app/licenses/THIRD_PARTY_NOTICES.md"
cp "$kit/THIRD_PARTY_NOTICES.md" "$app/licenses/ps5-homebrew-ui-THIRD_PARTY_NOTICES.md"
for d in "$T"/thirdparty/*/; do   # the licence files that ship with trellis.cpp's bundled sources (some keep theirs in the headers)
    for f in "$d"LICENSE*; do [ -f "$f" ] && cp "$f" "$app/licenses/$(basename "$d")-$(basename "$f")"; done
done
rev=$(git -C "$W" rev-parse HEAD 2> /dev/null || echo unknown)
cat > "$app/SOURCE.txt" <<EOT
PS5MG (title PPSA99577), built from https://github.com/nikay99/PS5MG at commit $rev
trellis.cpp 8e4ce11a77d35c3e3a4b3f84a29d92bf5491e64e + ggml f8697b33795db2ee6a2569b40853ca6aaa69f9b9 (see scripts/build-mg.sh)
No model weights are included: see the README (Licences) and tools/get-weights.sh.
EOT
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
echo "build-mg: $app ($(stat -c %s "$app/eboot.bin") bytes, $([ $display = 1 ] && echo with || echo without) a screen)"
