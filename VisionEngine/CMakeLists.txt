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

# OpenCV 런타임 DLL을 실행파일 폴더로 복사 (사전빌드 OpenCV 사용 시 런타임 로드용)
if(WIN32 AND DEFINED OpenCV_DIR)
    file(GLOB _ocv_dll "${OpenCV_DIR}/x64/vc16/bin/opencv_world*.dll")
    foreach(_dll ${_ocv_dll})
        if(NOT _dll MATCHES "d\\.dll$")   # 릴리스 DLL만 (디버그 *d.dll 제외)
            add_custom_command(TARGET VisionEngine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:VisionEngine>")
        endif()
    endforeach()
endif()
