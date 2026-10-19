cmake_minimum_required(VERSION 3.16)
project(ModelBridge LANGUAGES CXX)

# 统一使用标准 C++17，禁止回退到旧标准或启用编译器扩展。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 使用已安装的开发库，不在构建时自动下载依赖。
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

add_library(modelbridge_sdk STATIC src/LLMProvider.cpp)
target_include_directories(modelbridge_sdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(modelbridge_sdk
    PUBLIC spdlog::spdlog Threads::Threads
    PRIVATE CURL::libcurl JsonCpp::JsonCpp
)

add_executable(modelbridge src/main.cpp)
target_link_libraries(modelbridge_demo PRIVATE modelbridge_sdk)
