find_package(GTest CONFIG REQUIRED)

# ── Core 파이프라인 + 프레임 테스트 ─────────────────────────────────
add_executable(CoreTests
    Core/PipelineTest.cpp
    Core/FrameTest.cpp
)
target_link_libraries(CoreTests PRIVATE
    Core
    GTest::gtest_main
)
add_test(NAME CoreTests COMMAND CoreTests)

include(GoogleTest)
gtest_discover_tests(CoreTests)
