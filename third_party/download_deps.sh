#!/bin/bash
set -e

echo "下载第三方依赖库..."

cd "$(dirname "$0")"

# 下载 standalone asio
if [ ! -d "asio" ]; then
    echo "下载 asio..."
    wget -q https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-2.tar.gz
    tar -xzf asio-1-28-2.tar.gz
    mv asio-asio-1-28-2/asio asio
    rm -rf asio-asio-1-28-2 asio-1-28-2.tar.gz
    echo "asio 下载完成"
else
    echo "asio 目录已存在，跳过下载"
fi

# 下载 websocketpp
if [ ! -d "websocketpp" ]; then
    echo "下载 websocketpp..."
    wget -q https://github.com/zaphoyd/websocketpp/archive/refs/tags/0.8.2.tar.gz
    tar -xzf 0.8.2.tar.gz
    mv websocketpp-0.8.2 websocketpp
    rm -rf 0.8.2.tar.gz
    echo "websocketpp 下载完成"
else
    echo "websocketpp 目录已存在，跳过下载"
fi

# 下载 nlohmann/json
if [ ! -f "json.hpp" ]; then
    echo "下载 nlohmann/json..."
    wget -q https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    echo "nlohmann/json 下载完成"
else
    echo "json.hpp 已存在，跳过下载"
fi

# 下载 yaml-cpp (header-only version)
if [ ! -d "yaml-cpp" ]; then
    echo "下载 yaml-cpp..."
    wget -q https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.8.0.tar.gz
    tar -xzf yaml-cpp-0.8.0.tar.gz
    mkdir -p yaml-cpp
    cp -r yaml-cpp-yaml-cpp-0.8.0/include/yaml-cpp yaml-cpp/
    rm -rf yaml-cpp-yaml-cpp-0.8.0 yaml-cpp-0.8.0.tar.gz
    echo "yaml-cpp 下载完成"
else
    echo "yaml-cpp 目录已存在，跳过下载"
fi

# 下载 sherpa-onnx
if [ ! -d "sherpa-onnx" ]; then
    echo "下载 sherpa-onnx..."
    echo "注意：sherpa-onnx 仓库较大（约 300MB+），下载可能需要一些时间..."
    git clone --depth 1 --branch master https://github.com/k2-fsa/sherpa-onnx.git
    echo "sherpa-onnx 下载完成"
    echo ""
    echo "重要提示："
    echo "1. sherpa-onnx 需要编译，请按照 README.md 中的说明进行编译"
    echo "2. 编译 sherpa-onnx 需要安装以下依赖："
    echo "   - cmake >= 3.13"
    echo "   - gcc/clang with C++17 support"
    echo "   - pthread"
    echo ""
    echo "编译 sherpa-onnx 的步骤："
    echo "  cd third_party/sherpa-onnx"
    echo "  mkdir -p build && cd build"
    echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
    echo "  make -j\$(nproc)"
else
    echo "sherpa-onnx 目录已存在，跳过下载"
fi

echo ""
echo "所有依赖下载完成！"
echo "目录结构："
echo "  asio/         - asio 头文件 (用于 WebSocket 服务器)"
echo "  websocketpp/  - websocketpp 头文件 (用于 WebSocket 服务器)"
echo "  json.hpp      - nlohmann/json 单一头文件 (用于 JSON 序列化)"
echo "  yaml-cpp/     - yaml-cpp 头文件 (用于 YAML 配置文件)"
echo "  sherpa-onnx/  - sherpa-onnx 源码 (用于语音识别)"
