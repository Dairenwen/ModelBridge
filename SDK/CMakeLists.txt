# 异步日志需要线程支持；由 CMake 处理不同系统的线程链接参数。
find_package(Threads REQUIRED)

# 优先使用系统安装的 spdlog，导入目标会携带所需的 fmt 等依赖。
find_package(spdlog 1.10 CONFIG QUIET)
if(NOT TARGET spdlog::spdlog)
    # 未安装时获取固定版本；源码和编译产物均保存在 build 内。
    # 校验压缩包摘要，避免构建时使用意外变化的依赖内容。
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        URL "https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.15.3"
        URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# 当前 SDK 只有头文件，因此使用 INTERFACE 目标，无需创建空的静态库。
# 后续程序链接 ModelBridge::SDK，即可继承头文件目录、C++17 和依赖。
add_library(modelbridge_sdk INTERFACE)
add_library(ModelBridge::SDK ALIAS modelbridge_sdk)
target_include_directories(modelbridge_sdk INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(modelbridge_sdk INTERFACE cxx_std_17)
target_link_libraries(modelbridge_sdk INTERFACE spdlog::spdlog Threads::Threads)

# 编译现有 main.cpp，生成 build/bin/modelbridge_demo 日志示例。
add_executable(modelbridge_demo src/main.cpp)
target_link_libraries(modelbridge_demo PRIVATE ModelBridge::SDK)

# 只为项目代码开启常用警告，不修改第三方库的编译选项。
if(MSVC)
    target_compile_options(modelbridge_demo PRIVATE /W4 /utf-8)
else()
    target_compile_options(modelbridge_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
# SDK 是独立的 CMake 工程，在 SDK 目录执行 cmake -S . -B build。
cmake_minimum_required(VERSION 3.16...3.31)
project(ModelBridgeSDK LANGUAGES CXX)

# 统一使用标准 C++17，禁止回退到旧标准或启用编译器扩展。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 为 clangd 等工具生成编译数据库；可执行文件放入 SDK/build/bin。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

