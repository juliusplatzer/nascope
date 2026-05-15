find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGL)

if(NOT APPLE)
    find_package(OpenGL REQUIRED COMPONENTS GLU)
endif()

add_library(nascope_renderer STATIC
    command_buffer.cpp
    builders.cpp
    display_emulation.cpp
    font.cpp
    gl/display_emulation_pass.cpp
    gl/opengl_renderer.cpp
    gl/shaders.cpp
    gl/glu_tessellator.cpp
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
