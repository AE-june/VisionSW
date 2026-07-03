add_library(VisionTools STATIC
    src/NoiseFilter.cpp
    src/EdgeDetector.cpp
    src/ThicknessMeasure.cpp
    src/PlaneFitTool.cpp
    src/HeightFromPlaneTool.cpp
    src/CsvWriterTool.cpp
    src/LineCenterTool.cpp
    src/AlignTool.cpp
)

target_include_directories(VisionTools
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(VisionTools PUBLIC Core Eigen3::Eigen)
if(OpenMP_CXX_FOUND)
    target_link_libraries(VisionTools PUBLIC OpenMP::OpenMP_CXX)
endif()

# target_link_libraries(VisionTools PRIVATE ${OpenCV_LIBS} ${PCL_LIBRARIES})
