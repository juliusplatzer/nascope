find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(nascope_util STATIC
    resources.cpp
)

target_include_directories(nascope_util
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_util
    PUBLIC
        Qt6::Core
)
