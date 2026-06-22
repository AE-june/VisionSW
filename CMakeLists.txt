cmake_minimum_required(VERSION 3.20)
project(VisionSW VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# ── Core / VisionTools ───────────────────────────────────────────────
add_subdirectory(Core)
add_subdirectory(VisionTools)

# ── VisionEngine (WebSocket server) ──────────────────────────────────
add_subdirectory(VisionEngine)

enable_testing()
add_subdirectory(Tests)
