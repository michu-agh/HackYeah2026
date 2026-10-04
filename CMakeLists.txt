cmake_minimum_required(VERSION 3.20)
project(crisismesh_rpi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Crow 1.3.4 - current release used for this project.
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(Crow)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(crisismesh
    src/main.cpp
    src/SerialManager.cpp
)

target_include_directories(crisismesh PRIVATE src)

target_link_libraries(crisismesh
    PRIVATE
        Crow::Crow
        nlohmann_json::nlohmann_json
        pthread
)

target_compile_definitions(crisismesh PRIVATE
    CRISIS_WEB_DIR="${CMAKE_SOURCE_DIR}/web"
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(crisismesh PRIVATE -Wall -Wextra -Wpedantic)
endif()
