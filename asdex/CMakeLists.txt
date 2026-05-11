find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGLWidgets WebSockets)
find_package(ZLIB REQUIRED)

add_library(nascope_asdex STATIC
    window.cpp
    atis/atis_cache.cpp
    notams/runway_closure_cache.cpp
    input/datablock_edit_command.cpp
    math.cpp
    resources.cpp
    cursors.cpp
    videomap.cpp
    draw_video_map.cpp
    draw_runway_closures.cpp
    draw_temp_areas.cpp
    draw_targets.cpp
    draw_datablocks.cpp
    scope.cpp
    lists/screen_list.cpp
    lists/preview_area.cpp
    lists/coast_list.cpp
    targets/target_cache.cpp
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
    PRIVATE
        ZLIB::ZLIB
)

add_executable(asdex_scope main.cpp)
target_link_libraries(asdex_scope PRIVATE nascope_asdex)
