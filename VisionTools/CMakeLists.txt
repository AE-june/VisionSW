add_library(VisionTools STATIC
    src/NoiseFilter.cpp
    src/EdgeDetector.cpp
    src/ThicknessMeasure.cpp
    src/LineFitHeightMeasure.cpp
    src/PlaneFitTool.cpp
    src/HeightFromPlaneTool.cpp
)

target_include_directories(VisionTools
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VisionTools PUBLIC Core)

# target_link_libraries(VisionTools PRIVATE ${OpenCV_LIBS} ${PCL_LIBRARIES})
