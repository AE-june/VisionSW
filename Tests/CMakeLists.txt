find_package(GTest CONFIG REQUIRED)

# ── Core 파이프라인 테스트 ─────────────────────────────────────────
add_executable(CoreTests
    Core/PipelineTest.cpp
)
target_link_libraries(CoreTests PRIVATE
    Core
    GTest::gtest_main
)
add_test(NAME CoreTests COMMAND CoreTests)

# ── VisionTools 알고리즘 테스트 ───────────────────────────────────
add_executable(ToolTests
    Tools/ThicknessMeasureTest.cpp
    Tools/LineFitHeightMeasureTest.cpp
)
target_link_libraries(ToolTests PRIVATE
    VisionTools
    GTest::gtest_main
)
add_test(NAME ToolTests COMMAND ToolTests)

include(GoogleTest)
gtest_discover_tests(CoreTests)
gtest_discover_tests(ToolTests)
