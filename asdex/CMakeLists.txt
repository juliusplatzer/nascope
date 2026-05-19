find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(ZLIB REQUIRED)

add_library(nascope_asdex STATIC
    window.cpp
    notamcache.cpp
    cmdsetup.cpp
    cmdslew.cpp
    cursors.cpp
    dcb.cpp
    dbareas.cpp
    videomaps.cpp
    tempdata.cpp
    target.cpp
    datablock.cpp
    asdex.cpp
    lists.cpp
)

target_include_directories(nascope_asdex
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(nascope_asdex
    PUBLIC
        Qt6::Widgets
        Qt6::OpenGLWidgets
        nascope_renderer
        nascope_panes
        nascope_io
        nascope_math
        nascope_radar
        nascope_util
    PRIVATE
        ZLIB::ZLIB
)

add_executable(asdex_scope main.cpp)
target_link_libraries(asdex_scope PRIVATE nascope_asdex)
