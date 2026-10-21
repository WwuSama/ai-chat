cmake_minimum_required(VERSION 3.23)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(ai_box)

find_package(PkgConfig REQUIRED)

pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JSON-C REQUIRED IMPORTED_TARGET json-c)

add_executable(curlDemo)

target_sources(curlDemo
    PRIVATE
    src/curlDemo.c
)

target_link_libraries(curlDemo
    PRIVATE
    PkgConfig::CURL
)

add_executable(jsoncDemo)

target_sources(jsoncDemo
    PRIVATE
    src/jsoncDemo.c
)

target_link_libraries(jsoncDemo
    PRIVATE
    PkgConfig::JSON-C
)

add_executable(aiChat)

target_sources(aiChat
    PRIVATE
    src/aiChat.c
)

target_link_libraries(aiChat
    PRIVATE
    PkgConfig::CURL
    PkgConfig::JSON-C
)
