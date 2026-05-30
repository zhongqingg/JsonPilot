cmake_minimum_required(VERSION 3.18)
project(JsonPilot VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_language(RC)

add_subdirectory(thirdparty/webui)

set(WEB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
add_definitions(-DWEB_SOURCE_DIR="${WEB_SOURCE_DIR}")

configure_file(src/resources.rc.in src/resources.rc @ONLY)

if(WIN32)
    add_executable(JsonPilot WIN32
        src/main.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/src/resources.rc
    )
else()
    add_executable(JsonPilot
        src/main.cpp
    )
endif()

target_include_directories(JsonPilot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmannjson/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webui/include
)

target_link_libraries(JsonPilot PRIVATE webui ole32 runtimeobject shell32)

if(MSVC)
    set_target_properties(JsonPilot PROPERTIES
        VS_DPI_AWARE "ON"
    )
endif()

# Copy web folder to build output directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/web DESTINATION ${CMAKE_BINARY_DIR})

add_custom_command(TARGET JsonPilot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/web"
        "$<TARGET_FILE_DIR:JsonPilot>/web"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/icon.png"
        "$<TARGET_FILE_DIR:JsonPilot>/web/icon.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/icon.ico"
        "$<TARGET_FILE_DIR:JsonPilot>/web/icon.ico"
    COMMENT "Copy web files to output directory"
)

# Install / Packaging target
include(GNUInstallDirs)
install(TARGETS JsonPilot RUNTIME DESTINATION .)
install(DIRECTORY web/ DESTINATION web)
install(FILES src/icon.png DESTINATION web)
install(FILES src/icon.ico DESTINATION web)
install(DIRECTORY data/ DESTINATION data OPTIONAL)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Release/config.txt DESTINATION . OPTIONAL)
install(CODE "
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/config.txt\" \"root_dir=data\ntheme=dark\n\")
")

# Custom target for deploy packaging
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config Release --prefix ${CMAKE_CURRENT_SOURCE_DIR}/deploy/JsonPilot
    COMMENT "Packaging JsonPilot to deploy folder"
)

message(STATUS "Web folder: ${CMAKE_CURRENT_SOURCE_DIR}/web")
