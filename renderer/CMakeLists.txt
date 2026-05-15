find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGL)

if(NOT APPLE)
    find_package(OpenGL REQUIRED COMPONENTS GLU)
endif()

add_library(nascope_renderer STATIC
    cmdbuffer.cpp
    builders.cpp
    font.cpp
    gl/ogl.cpp
    gl/shaders.cpp
    gl/glutessellator.cpp
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

if(APPLE)
    target_link_libraries(nascope_renderer PRIVATE "-framework OpenGL")
    target_compile_definitions(nascope_renderer PRIVATE GL_SILENCE_DEPRECATION)
else()
    target_link_libraries(nascope_renderer PRIVATE OpenGL::GLU)
endif()
