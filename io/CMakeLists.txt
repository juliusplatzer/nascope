find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets)

add_library(nascope_io STATIC
    smes.cpp
    atis.cpp
)

target_include_directories(nascope_io
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_io
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
        nascope_util
)
