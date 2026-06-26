cmake_minimum_required(VERSION 3.18)
project(JsonPilot VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Static MSVC runtime — no vcruntime140.dll dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

enable_language(RC)

set(WEBUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/webui)

set(WEB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
add_definitions(-DWEB_SOURCE_DIR="${WEB_SOURCE_DIR}")

configure_file(src/resources.rc.in src/resources.rc @ONLY)

# ── Backend target (headless HTTP server) ──
if(WIN32)
    add_executable(JsonPilotBackend WIN32
        src/main.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/src/resources.rc
    )
else()
    add_executable(JsonPilotBackend
        src/main.cpp
    )
endif()

target_include_directories(JsonPilotBackend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmannjson/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webui/include
)

target_link_libraries(JsonPilotBackend PRIVATE webui ole32 runtimeobject shell32)

if(MSVC)
    set_target_properties(JsonPilotBackend PROPERTIES
        VS_DPI_AWARE "ON"
    )
endif()

# ── Viewer target (WebView2 window) ──
if(WIN32)
    add_executable(JsonPilotViewer WIN32
        src/viewer_main.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/src/resources.rc
    )
endif()

target_include_directories(JsonPilotViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webview2/include
)

target_link_libraries(JsonPilotViewer PRIVATE
    dwmapi ole32 runtimeobject shell32
)

# Copy WebView2Loader.dll to viewer output
add_custom_command(TARGET JsonPilotViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:JsonPilotViewer>/WebView2Loader.dll"
    COMMENT "Copy WebView2Loader.dll to output"
)

# Copy web folder to both output directories
foreach(tgt JsonPilotBackend JsonPilotViewer)
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/web"
            "$<TARGET_FILE_DIR:${tgt}>/web"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/icon.png"
            "$<TARGET_FILE_DIR:${tgt}>/web/icon.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/icon.ico"
            "$<TARGET_FILE_DIR:${tgt}>/web/icon.ico"
        COMMENT "Copy web files to ${tgt} output"
    )
endforeach()

# Install rules
include(GNUInstallDirs)
install(TARGETS JsonPilotBackend JsonPilotViewer RUNTIME DESTINATION .)
install(FILES thirdparty/WebView2Loader.dll DESTINATION .)
install(DIRECTORY web/ DESTINATION web)
install(FILES src/icon.png DESTINATION web)
install(FILES src/icon.ico DESTINATION web)
install(DIRECTORY data/ DESTINATION data OPTIONAL)

# Default config
install(CODE "
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/config.txt\"
        \"root_dir=data\\ntheme=dark\\n\")
")

# Custom deploy target
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config Release
        --prefix ${CMAKE_CURRENT_SOURCE_DIR}/deploy/JsonPilot
    COMMENT "Packaging to deploy/JsonPilot/"
)

message(STATUS "Web folder: ${CMAKE_CURRENT_SOURCE_DIR}/web")
message(STATUS "Targets: JsonPilotBackend, JsonPilotViewer")
