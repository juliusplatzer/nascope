find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGL OpenGLWidgets WebSockets)
find_package(ZLIB REQUIRED)

if(NOT APPLE)
    find_package(OpenGL REQUIRED COMPONENTS GLU)
endif()

add_library(nascope_asdex STATIC
    window.cpp
    atis/atis_cache.cpp
    notams/runway_closure_cache.cpp
    input/datablock_edit_command.cpp
    render/math.cpp
    render/resources.cpp
    render/cursors.cpp
    render/screen_line_renderer.cpp
    render/videomap.cpp
    render/runway_closures.cpp
    render/temp_areas.cpp
    render/targets.cpp
    render/datablocks.cpp
    render/scope.cpp
    lists/screen_list.cpp
    lists/preview_area.cpp
    lists/coast_list.cpp
    targets/target_cache.cpp
)

target_include_directories(nascope_asdex
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_asdex
    PUBLIC
        Qt6::Widgets
        Qt6::Network
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::WebSockets
        nascope_renderer
    PRIVATE
        ZLIB::ZLIB
)

if(APPLE)
    target_link_libraries(nascope_asdex PRIVATE "-framework OpenGL")
    target_compile_definitions(nascope_asdex PRIVATE GL_SILENCE_DEPRECATION)
else()
    target_link_libraries(nascope_asdex PRIVATE OpenGL::GLU)
endif()

add_executable(asdex_scope main.cpp)
target_link_libraries(asdex_scope PRIVATE nascope_asdex)
