cmake_minimum_required(VERSION 3.20)
project(VisionSW VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: 소스/실행파일 인코딩을 UTF-8로 강제 (한글 문자열 EUC-KR→UTF-8 오류 방지)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ─────────────────────────────────────────────────────────────────────
#  빌드 예시:
#    cmake -B build \
#      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake \
#      -DVCPKG_TARGET_TRIPLET=x64-windows \
#      -G "Visual Studio 17 2022" -A x64
# ─────────────────────────────────────────────────────────────────────

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── vcpkg packages ────────────────────────────────────────────────────
find_package(spdlog        CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest         CONFIG REQUIRED)
find_package(Eigen3        CONFIG REQUIRED)
find_package(OpenMP)

# ── Core / VisionTools ───────────────────────────────────────────────
add_subdirectory(Core)
add_subdirectory(VisionTools)

# ── VisionEngine (WebSocket server) ──────────────────────────────────
add_subdirectory(VisionEngine)

# ── VisionSDK (모든 노드를 C ABI DLL로 노출 — C++/C# 외부 사용) ────────
add_subdirectory(VisionSDK)

enable_testing()
add_subdirectory(Tests)
