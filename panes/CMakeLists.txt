find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_library(nascope_panes STATIC
    panes.cpp
    display.cpp
)

target_include_directories(nascope_panes
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_panes
    PUBLIC
        Qt6::Widgets
        Qt6::OpenGLWidgets
        nascope_renderer
)
