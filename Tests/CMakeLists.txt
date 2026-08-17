find_package(GTest CONFIG REQUIRED)

# ── Core 파이프라인 + 프레임 테스트 ─────────────────────────────────
add_executable(CoreTests
    Core/PipelineTest.cpp
    Core/FrameTest.cpp
    Core/BroadcastTest.cpp
)
target_link_libraries(CoreTests PRIVATE
    Core
    GTest::gtest_main
)
add_test(NAME CoreTests COMMAND CoreTests)

# ── VisionTools 단위 테스트 ──────────────────────────────────────────
add_executable(ToolsTests
    Tools/ValidRegionTest.cpp
    Tools/LevelToolTest.cpp
    Tools/SurfaceCropTest.cpp
    Tools/SurfaceResampleTest.cpp
    Tools/SurfaceSubtractTest.cpp
    Tools/PlaneFitRegionTest.cpp
    Tools/LineFitTest.cpp
    Tools/CreateRoiBandTest.cpp
    Tools/ReduceDomainTest.cpp
    Tools/CloudToProfilesTest.cpp
    Tools/NoiseFilterRegionTest.cpp
    Tools/AggregateTest.cpp
    Tools/ExtractProfileTest.cpp
    Tools/ProfileFeatureTest.cpp
    Tools/CompareToolTest.cpp
    Tools/RegionMeasureTest.cpp
    Tools/HeightMapMetaTest.cpp
    Tools/InvariantTests.cpp
)
target_link_libraries(ToolsTests PRIVATE
    VisionTools
    GTest::gtest_main
)
add_test(NAME ToolsTests COMMAND ToolsTests)

include(GoogleTest)
gtest_discover_tests(CoreTests)
gtest_discover_tests(ToolsTests)
