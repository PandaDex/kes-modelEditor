cmake_minimum_required(VERSION 3.21)
project(kesModelEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
qt_standard_project_setup()

# Uses the engine's own loaders/savers and anim code so output matches the game exactly
set(KES_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../engine")
add_library(kme_engine STATIC
    ${KES_ENGINE_DIR}/kmodel.cpp
    ${KES_ENGINE_DIR}/animation_clip.cpp
    ${KES_ENGINE_DIR}/animation_pose.cpp
    ${KES_ENGINE_DIR}/anim_blend_tree.cpp
    ${KES_ENGINE_DIR}/text_block_file.cpp
    src/EngineFileIo.cpp
)
target_include_directories(kme_engine PUBLIC ${KES_ENGINE_DIR})
target_compile_definitions(kme_engine PUBLIC KES_PROFILING=0 _CRT_SECURE_NO_WARNINGS NOMINMAX)

add_library(kme_core STATIC
    src/KesProject.h
    src/KesProject.cpp
    src/ModelDocument.h
    src/ModelDocument.cpp
    src/graph/Types.h
    src/graph/NodeLibrary.h
    src/graph/NodeLibrary.cpp
    src/graph/GraphModel.h
    src/graph/GraphModel.cpp
)
target_include_directories(kme_core PUBLIC src src/graph)
target_link_libraries(kme_core PUBLIC kme_engine Qt6::Core Qt6::Gui)

qt_add_executable(kesModelEditor
    src/main.cpp
    src/KesStyle.h
    src/KesStyle.cpp
    src/PreviewState.h
    src/PreviewState.cpp
    src/ModelViewport.h
    src/ModelViewport.cpp
    src/SkeletonPanel.h
    src/SkeletonPanel.cpp
    src/AttachmentPanel.h
    src/AttachmentPanel.cpp
    src/AnimationPanel.h
    src/AnimationPanel.cpp
    src/GraphParamsPanel.h
    src/GraphParamsPanel.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/graph/GraphItems.h
    src/graph/GraphItems.cpp
    src/graph/NodeGraphScene.h
    src/graph/NodeGraphScene.cpp
    src/graph/NodeGraphView.h
    src/graph/NodeGraphView.cpp
    src/graph/NodeSearchPopup.h
    src/graph/NodeSearchPopup.cpp
    src/graph/InspectorWidget.h
    src/graph/InspectorWidget.cpp
)
target_link_libraries(kesModelEditor PRIVATE kme_core Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

qt_add_resources(kesModelEditor "app_icon"
    PREFIX "/icons"
    BASE resource
    FILES resource/icon.png
)

set_target_properties(kesModelEditor PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(MSVC)
    target_compile_options(kesModelEditor PRIVATE /W4 /utf-8)
    target_compile_options(kme_core PRIVATE /W4 /utf-8)
    target_compile_options(kme_engine PRIVATE /utf-8)
else()
    target_compile_options(kesModelEditor PRIVATE -Wall -Wextra)
    target_compile_options(kme_core PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    target_sources(kesModelEditor PRIVATE resource/kesModelEditor.rc)
endif()

install(TARGETS kesModelEditor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

include(CTest)
if(BUILD_TESTING)
    add_executable(editor_test tests/editor_test.cpp)
    target_link_libraries(editor_test PRIVATE kme_core)
    add_test(NAME editor_test COMMAND editor_test)
    if(WIN32)
        set_tests_properties(editor_test PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${Qt6_DIR}/../../../bin")
    endif()
endif()
