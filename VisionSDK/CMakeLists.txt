# VisionSDK — 모든 노드를 C ABI로 노출하는 공유 라이브러리(DLL).
#  ToolFactory.cpp를 함께 컴파일해 팩토리가 아는 모든 노드(로더/머지/필터/측정/저장)를 포함.
add_library(VisionSDK SHARED
    src/vision_sdk.cpp
    ${CMAKE_SOURCE_DIR}/VisionEngine/src/ToolFactory.cpp
)

target_include_directories(VisionSDK
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/VisionEngine/src   # ToolFactory.h, ZMapCache.h 등
)

# Core/VisionTools가 OpenCV/Eigen/spdlog/nlohmann을 PUBLIC 전파.
target_link_libraries(VisionSDK PRIVATE
    Core
    VisionTools
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

target_compile_definitions(VisionSDK PRIVATE VISIONSDK_EXPORTS)

# stb (ToolFactory의 이미지 로더)
find_package(Stb REQUIRED)
target_include_directories(VisionSDK PRIVATE ${Stb_INCLUDE_DIR})

if(MSVC)
    target_compile_definitions(VisionSDK PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# OpenCV 런타임 DLL을 VisionSDK.dll 옆으로 복사 (배포 시 함께 필요)
if(WIN32 AND DEFINED OpenCV_DIR)
    file(GLOB _ocv_dll "${OpenCV_DIR}/x64/vc16/bin/opencv_world*.dll")
    foreach(_dll ${_ocv_dll})
        if(NOT _dll MATCHES "d\\.dll$")
            add_custom_command(TARGET VisionSDK POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:VisionSDK>")
        endif()
    endforeach()
endif()

# ── SDK 스모크 테스트 (C++에서 DLL 함수 직접 호출) ──
add_executable(VisionSDKTest test/sdk_test.cpp)
target_link_libraries(VisionSDKTest PRIVATE VisionSDK)
target_include_directories(VisionSDKTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
