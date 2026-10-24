cmake_minimum_required(VERSION 3.16)
project(ModelBridgeExample LANGUAGES CXX)

find_package(ModelBridge CONFIG REQUIRED)
add_executable(chat chat.cpp)
target_link_libraries(chat PRIVATE ModelBridge::modelbridge_sdk)
