find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(nascope_asdex_shell STATIC
    asdex_shell.cpp
)

target_include_directories(nascope_asdex_shell
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_asdex_shell
    PUBLIC
        Qt6::Widgets
        nascope_renderer
)

add_executable(asdex_scope main.cpp)
target_link_libraries(asdex_scope PRIVATE nascope_asdex_shell)
