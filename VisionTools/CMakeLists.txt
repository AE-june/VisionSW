add_library(VisionTools STATIC
    src/Aggregate.cpp
    src/CompareTool.cpp
    src/CombineDecisionTool.cpp
    src/ExtractProfileTool.cpp
    src/ProfileFeatureTool.cpp
    src/NoiseFilter.cpp
    src/PlaneFitTool.cpp
    src/LineFitTool.cpp
    src/HeightMapToCloudTool.cpp
    src/CloudLoaderTool.cpp
    src/CloudToProfilesTool.cpp
    src/RowStretchTool.cpp
    src/GapFillTool.cpp
    src/CsvWriterTool.cpp
    src/LineCenterTool.cpp
    src/AlignTool.cpp
    src/ThresholdTool.cpp
    src/CreateRoiTool.cpp
    src/ReduceDomainTool.cpp
    src/RegionMeasureTool.cpp
    src/ValidRegionTool.cpp
    src/LevelTool.cpp
    src/SurfaceCropTool.cpp
    src/SurfaceResampleTool.cpp
    src/SurfaceSubtractTool.cpp
)

target_include_directories(VisionTools
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 사전빌드된 OpenCV 사용 (D:\Libraries\opencv). vcpkg 빌드 불필요.
if(NOT DEFINED OpenCV_DIR)
    set(OpenCV_DIR "D:/Libraries/opencv/build" CACHE PATH "Prebuilt OpenCV dir")
endif()
find_package(OpenCV REQUIRED)

target_link_libraries(VisionTools PUBLIC Core Eigen3::Eigen)
target_link_libraries(VisionTools PUBLIC ${OpenCV_LIBS})
target_include_directories(VisionTools PUBLIC ${OpenCV_INCLUDE_DIRS})
if(OpenMP_CXX_FOUND)
    target_link_libraries(VisionTools PUBLIC OpenMP::OpenMP_CXX)
endif()
