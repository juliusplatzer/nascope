find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGL)

add_library(nascope_renderer STATIC
    text/bitmap_font.cpp
    text/bitmap_font_renderer.cpp
)

target_include_directories(nascope_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_renderer
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGL
)
