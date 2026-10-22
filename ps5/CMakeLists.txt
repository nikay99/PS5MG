# PS5MG: trellis.cpp (CPU + ggml-vulkan on RADV) as static libraries for a PS5 title.
# Not trellis.cpp's own CMakeLists: no executables, no WebP, none of its direct-Vulkan
# helper kernels (deform conv and QEM decimation fall back to their CPU code).
# scripts/build-mg.sh configures this with PS5SD's toolchain.cmake.
cmake_minimum_required(VERSION 3.18)
project(ps5mg C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(T ${TRELLIS_SRC})
add_compile_definitions(GGML_MAX_NAME=128)
add_subdirectory(${T}/thirdparty/ggml ggml)

set(CORE
  trellis_debug trellis_sched trellis_model dit flow_runner ss_decoder mesh_glb dinov3 pixal3d naf sparse
  shape_decoder dual_grid preprocess uv_bake uv_overlap_cover birefnet tri_bvh remesh_dc decimate_qem
  mesh_shape quad_refinement quad_refinement_gates quad_refinement_io quad_components quad_topology
  quad_component_cover trellis_cli trellis_args)
list(TRANSFORM CORE PREPEND ${T}/src/)
list(TRANSFORM CORE APPEND .cpp)
add_library(trellis_core STATIC ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/deform_conv_fast.cpp ${CMAKE_CURRENT_SOURCE_DIR}/decimate_qem_fast.cpp ${T}/thirdparty/xatlas/xatlas.cpp ${T}/thirdparty/meshoptimizer/simplifier.cpp)
# The original QEM simplifier stays, under another name, for the env switches the fast one does not do.
set_source_files_properties(${T}/src/decimate_qem.cpp PROPERTIES COMPILE_DEFINITIONS "decimate_qem=decimate_qem_orig")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/deform_conv_fast.cpp PROPERTIES COMPILE_OPTIONS "-O3;-mavx2;-mfma")
target_include_directories(trellis_core PUBLIC ${T}/include ${T}/thirdparty/ggml/include ${T}/thirdparty/stb
  ${T}/thirdparty/xatlas ${T}/thirdparty/meshoptimizer ${T}/thirdparty/fqms)
target_compile_definitions(trellis_core PRIVATE TRELLIS_BUILD_BACKEND="vulkan" TRELLIS_GIT_COMMIT="ps5mg" TRELLIS_GIT_DIRTY=0
  TRELLIS_BUILD_TIMESTAMP="0")
target_compile_options(trellis_core PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/ps5mg_threads.h)
target_link_libraries(trellis_core PUBLIC ggml ggml-base ggml-cpu ggml-vulkan)

# The CLI's main() becomes a function the title calls.
add_library(trellis_cli_main STATIC ${T}/src/trellis_cli_main.cpp)
target_compile_definitions(trellis_cli_main PRIVATE main=trellis_cli_main)
target_link_libraries(trellis_cli_main PRIVATE trellis_core)
