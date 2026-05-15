find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGLWidgets WebSockets)
find_package(ZLIB REQUIRED)

add_library(nascope_asdex STATIC
    window.cpp
    atiscache.cpp
    notamcache.cpp
    cmdsetup.cpp
    cmdslew.cpp
    cursors.cpp
    dcb.cpp
    videomaps.cpp
    tempdata.cpp
    targets.cpp
    datablocks.cpp
    scope.cpp
    lists.cpp
    targetcache.cpp
)

target_include_directories(nascope_asdex
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_asdex
    PUBLIC
        Qt6::Widgets
        Qt6::Network
        Qt6::OpenGLWidgets
        Qt6::WebSockets
        nascope_renderer
        nascope_utils
    PRIVATE
        ZLIB::ZLIB
)

add_executable(asdex_scope main.cpp)
target_link_libraries(asdex_scope PRIVATE nascope_asdex)
