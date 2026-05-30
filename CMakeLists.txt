cmake_minimum_required(VERSION 3.18)
project(jsoneditor VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(thirdparty/webui)

set(WEB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
add_definitions(-DWEB_SOURCE_DIR="${WEB_SOURCE_DIR}")

if(WIN32)
    add_executable(jsoneditor WIN32
        src/main.cpp
    )
else()
    add_executable(jsoneditor
        src/main.cpp
    )
endif()

target_include_directories(jsoneditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmannjson
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webui/include
)

target_link_libraries(jsoneditor PRIVATE webui ole32 runtimeobject shell32)

if(MSVC)
    # Use CONSOLE for debug builds, WINDOWS for release
    set_target_properties(jsoneditor PROPERTIES
        VS_DPI_AWARE "ON"
    )
endif()

# Copy web folder to build output directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/web DESTINATION ${CMAKE_BINARY_DIR})

add_custom_command(TARGET jsoneditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/web"
        "$<TARGET_FILE_DIR:jsoneditor>/web"
    COMMENT "Copy web files to output directory"
)

message(STATUS "Web folder: ${CMAKE_CURRENT_SOURCE_DIR}/web")
