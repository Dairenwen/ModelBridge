# MODELBRIDGE(3)

## NAME

ModelBridge — C++17 大模型接口、工具调用、会话管理与 SQLite 持久化 SDK。

## SYNOPSIS

```text
chat MODEL MESSAGE [SESSION_ID]
```

## REQUIREMENTS

二进制包：Ubuntu 24.04 ARM64。其他 Linux 架构使用源码包编译。

```sh
sudo apt install build-essential cmake libspdlog-dev libjsoncpp-dev libssl-dev libsqlite3-dev
```

## INSTALL

```sh
tar -xzf ModelBridge-ubuntu24.04-aarch64.tar.gz
cd ModelBridge-ubuntu24.04-aarch64
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
sudo make -C build install
```

## BUILD FROM SOURCE

源码包包含 SDK 源码和 cpp-httplib 头文件。

```sh
tar -xzf ModelBridge-source.tar.gz
cd ModelBridge-source
cmake -S SDK -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DHTTPLIB_INCLUDE_DIR="$PWD/third_party/cpp-httplib" -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
sudo make -C build install
```

## USAGE

示例需要已启动的 Ollama 和已安装的模型。以下模型名称按本机情况替换。

```sh
cmake -S examples -B example-build
cmake --build example-build -j
./example-build/chat qwen2.5:7b-instruct-q4_K_M "你好"
./example-build/chat qwen2.5:7b-instruct-q4_K_M "继续上次的话题" "上一步输出的会话ID"
```

自己的 CMake 项目链接：

```cmake
find_package(ModelBridge CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE ModelBridge::modelbridge_sdk)
```

完整调用见 `examples/chat.cpp`。自定义安装位置时，在示例配置命令中增加 `-DCMAKE_PREFIX_PATH=/你的安装目录`。

## ENVIRONMENT

`MODELBRIDGE_OLLAMA_URL`：示例的模型地址，默认 `http://127.0.0.1:11434`。

## FILES

```text
/usr/local/lib/libmodelbridge_sdk.a          静态库
/usr/local/include/ModelBridge/             公开头文件
/usr/local/lib/cmake/ModelBridge/            CMake 包配置
./DBFiles/modelbridge.db                    示例会话数据库，相对于运行目录
```

## LICENSE

见包内 `LICENSE`、`NOTICE`。发布文件校验：`sha256sum -c SHA256SUMS`。
