add_executable(VisionEngine
    src/main.cpp
    src/ToolFactory.cpp
    src/JsonBridge.cpp
)

target_include_directories(VisionEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VisionEngine PRIVATE
    Core
    VisionTools
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# crow WebSocket server
find_package(Crow CONFIG REQUIRED)
target_link_libraries(VisionEngine PRIVATE Crow::Crow)

# stb (header-only image loading)
find_package(Stb REQUIRED)
target_include_directories(VisionEngine PRIVATE ${Stb_INCLUDE_DIR})

# Windows sockets
if(WIN32)
    target_link_libraries(VisionEngine PRIVATE ws2_32 wsock32)
endif()

# Suppress MSVC warnings about deprecated functions
if(MSVC)
    target_compile_definitions(VisionEngine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
