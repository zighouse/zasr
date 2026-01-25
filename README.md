# ZASR Server

基于 sherpa-onnx 的实时语音识别服务器，使用 WebSocket 协议提供语音识别服务。

## 项目结构

```
zasr/
├── src/                    # 源代码
├── third_party/           # 第三方依赖
│   ├── sherpa-onnx       # sherpa-onnx 源码
│   ├── asio/             # 独立 ASIO 库
│   ├── websocketpp/      # WebSocket++ 库
│   ├── json.hpp          # nlohmann/json 库
│   └── download_deps.sh  # 依赖下载脚本
└── build/                # 构建输出目录
    └── zasr-server      # 编译好的可执行文件
```

## 依赖

### 必需依赖

- sherpa-onnx 源码（需要预先编译）
- CMake >= 3.13
- C++17 编译器（GCC 7+, Clang 5+）
- pthread

### 自动下载的依赖

运行 `third_party/download_deps.sh` 会自动下载：
- standalone asio
- websocketpp
- nlohmann/json

## 编译步骤

### 1. 下载第三方依赖

```bash
cd third_party
bash download_deps.sh
```

### 2. 准备 sherpa-onnx

确保 `sherpa-onnx` 目录存在并且已经编译：

```bash
cd sherpa-onnx
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 3. 编译 zasr-server

```bash
cd zasr
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

编译完成后，可执行文件位于 `build/zasr-server`。

## 静态链接

zasr-server 静态链接了以下 sherpa-onnx 组件：
- sherpa-onnx-cxx-api
- sherpa-onnx-c-api
- sherpa-onnx-core
- sherpa-onnx-fst
- sherpa-onnx-kaldifst-core
- kaldi-decoder-core
- kaldi-native-fbank-core
- piper_phonemize
- espeak-ng
- ucd
- sentencepiece
- cargs
- kissfft
- onnxruntime

最终可执行文件约 35MB，不依赖任何外部动态库（除了系统标准库）。

## 使用方法

### 基本用法

```bash
./zasr-server \
  --silero-vad-model /models/k2-fsa/silero_vad.onnx \
  --sense-voice-model /models/k2-fsa/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/model.int8.onnx \
  --tokens /models/k2-fsa/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/tokens.txt \
  --port 2026
```

### 配置选项

**服务器配置**
- `--host` - 服务器地址（默认：0.0.0.0）
- `--port` - 服务器端口（默认：2026）
- `--max-connections` - 最大并发连接数（默认：256）
- `--worker-threads` - 工作线程数（默认：4）

**VAD 配置**
- `--silero-vad-model` - Silero VAD 模型路径（必需）
- `--vad-threshold` - VAD 阈值（默认：0.5）
- `--min-silence-duration` - 最小静音持续时间（默认：0.1s）
- `--min-speech-duration` - 最小语音持续时间（默认：0.25s）
- `--max-speech-duration` - 最大语音持续时间（默认：8.0s）

**ASR 配置**
- `--sense-voice-model` - SenseVoice 模型路径（必需）
- `--tokens` - tokens.txt 路径（必需）
- `--use-itn` - 使用逆文本标准化（默认：1）
- `--num-threads` - ASR 计算线程数（默认：2）

**处理配置**
- `--vad-window-size-ms` - VAD 窗口大小（默认：30ms）
- `--update-interval-ms` - 结果更新间隔（默认：200ms）
- `--max-batch-size` - 最大批处理大小（默认：5）

**日志和存储**
- `--log-file` - 日志文件路径（默认：stdout）
- `--data-dir` - 音频和识别结果保存目录

**超时配置**
- `--connection-timeout` - 连接超时（默认：15s）
- `--recognition-timeout` - 识别超时（默认：30s）

## 特性

1. **使用 sherpa-onnx 公共 API** - 不依赖 sherpa-onnx 内部接口
2. **完全静态链接** - 可执行文件独立运行
3. **实时流式识别** - 支持 WebSocket 流式音频传输
4. **VAD 断句** - 自动检测语音片段并断句
5. **中间结果** - 实时返回识别中间结果

## 与 sherpa-onnx 版本的关系

本项目使用 sherpa-onnx 的公共 C++ API（`cxx-api.h`），而不是内部 C++ 源码接口。这样可以：

- 避免依赖 sherpa-onnx 内部实现细节
- 兼容 sherpa-onnx API 变化
- 独立升级 sherpa-onnx 版本

当前兼容 sherpa-onnx 的编译版本（通过符号链接到本地 `third_party/sherpa-onnx`）。
