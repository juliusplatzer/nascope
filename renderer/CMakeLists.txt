find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets WebSockets)
find_package(ZLIB REQUIRED)

if(NOT APPLE)
    find_package(OpenGL REQUIRED COMPONENTS GLU)
endif()

add_library(nascope_renderer STATIC
    asdex_resources.cpp
    asdex_math.cpp
    asdex_cursors.cpp
    text/bitmap_font.cpp
    text/bitmap_font_renderer.cpp
    ../asdex/lists/screen_list.cpp
    ../asdex/lists/preview_area.cpp
    ../asdex/targets/target_cache.cpp
    videomap.cpp
    targets.cpp
    asdex_scope_widget.cpp
)

target_include_directories(nascope_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_renderer
    PUBLIC
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::WebSockets
    PRIVATE
        ZLIB::ZLIB
)

if(APPLE)
    target_link_libraries(nascope_renderer PRIVATE "-framework OpenGL")
    target_compile_definitions(nascope_renderer PRIVATE GL_SILENCE_DEPRECATION)
else()
    target_link_libraries(nascope_renderer PRIVATE OpenGL::GLU)
endif()
