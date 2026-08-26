add_library(Core STATIC
    src/VisionData.cpp
    src/Pipeline.cpp
    src/PipelineSerializer.cpp
    src/HeightMapLoader.cpp
    src/BroadcastRun.cpp
)

target_include_directories(Core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Core
    PUBLIC  spdlog::spdlog
            nlohmann_json::nlohmann_json
)
