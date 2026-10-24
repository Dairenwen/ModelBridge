cmake_minimum_required(VERSION 3.16)
project(ModelBridge LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 统一使用标准 C++17，禁止回退到旧标准或启用编译器扩展。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 使用已安装的开发库，不在构建时自动下载依赖。
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_path(HTTPLIB_INCLUDE_DIR NAMES httplib.h)

# 使用同一份带注释的 SQL 初始化数据库，避免运行时查找源码文件。
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" MODELBRIDGE_SCHEMA_SQL)
configure_file(src/DataManagerSchema.hpp.in generated/DataManagerSchema.hpp @ONLY)

file(GLOB_RECURSE SDK_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
file(GLOB_RECURSE SDK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)


list(REMOVE_ITEM SDK_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(modelbridge_sdk STATIC ${SDK_SOURCES} ${SDK_HEADERS})
add_library(ModelBridge::modelbridge_sdk ALIAS modelbridge_sdk)
target_compile_features(modelbridge_sdk PUBLIC cxx_std_17)
target_include_directories(modelbridge_sdk
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ModelBridge>"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${HTTPLIB_INCLUDE_DIR}"
)

target_compile_definitions(modelbridge_sdk PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(modelbridge_sdk PUBLIC
    spdlog::spdlog
    Threads::Threads
    JsonCpp::JsonCpp
    SQLite::SQLite3
    PRIVATE OpenSSL::SSL
    OpenSSL::Crypto
)

# 静态库
set(MODELBRIDGE_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/ModelBridge")
configure_package_config_file(
    cmake/ModelBridgeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/ModelBridgeConfig.cmake"
    INSTALL_DESTINATION "${MODELBRIDGE_CMAKE_INSTALL_DIR}"
)
install(TARGETS modelbridge_sdk EXPORT ModelBridgeTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES ${SDK_HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ModelBridge")
install(EXPORT ModelBridgeTargets NAMESPACE ModelBridge::
    DESTINATION "${MODELBRIDGE_CMAKE_INSTALL_DIR}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ModelBridgeConfig.cmake"
    DESTINATION "${MODELBRIDGE_CMAKE_INSTALL_DIR}"
)
install(FILES schema.sql DESTINATION "${CMAKE_INSTALL_DATADIR}/ModelBridge")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/ModelBridge"
)

add_executable(modelbridge src/main.cpp)
target_link_libraries(modelbridge PRIVATE modelbridge_sdk)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    foreach(TEST_SOURCE IN LISTS TEST_SOURCES)
        string(REGEX REPLACE "\\.cpp$" "" TEST_NAME "${TEST_SOURCE}")
        string(REPLACE "/" "_" TEST_NAME "${TEST_NAME}")
        add_executable(${TEST_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/tests/${TEST_SOURCE}")
        target_link_libraries(${TEST_NAME} PRIVATE modelbridge_sdk GTest::gtest_main)
        # 一个源文件对应一个 CTest 项，文件内部的用例由 GoogleTest 运行。
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()
