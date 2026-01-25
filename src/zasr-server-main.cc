#include <iostream>
#include <csignal>
#include <cstdlib>
#include <memory>

#include "zasr-server.h"
#include "zasr-config.h"

using namespace zasr;

std::unique_ptr<ZAsrServer> g_server;

// 信号处理函数
void SignalHandler(int signal) {
  std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
  if (g_server) {
    g_server->Stop();
  }
  std::exit(0);
}

// 注册信号处理
void RegisterSignalHandlers() {
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);
  
  // 忽略SIGPIPE信号（防止因客户端断开导致程序崩溃）
  std::signal(SIGPIPE, SIG_IGN);
}

// 打印用法
void PrintUsage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [options]\n\n";
  std::cout << "Options:\n";
  std::cout << "  --host <address>           Server host address (default: 0.0.0.0)\n";
  std::cout << "  --port <port>              Server port (default: 2026)\n";
  std::cout << "  --max-connections <num>    Maximum concurrent connections (default: 256)\n";
  std::cout << "  --worker-threads <num>     Number of worker threads (default: 4)\n";
  std::cout << "\n";
  std::cout << "  --silero-vad-model <path>  Path to Silero VAD model file (required)\n";
  std::cout << "  --vad-threshold <value>    VAD threshold (0.0-1.0, default: 0.5)\n";
  std::cout << "  --min-silence-duration <s> Minimum silence duration in seconds (default: 0.1)\n";
  std::cout << "  --min-speech-duration <s>  Minimum speech duration in seconds (default: 0.25)\n";
  std::cout << "  --max-speech-duration <s>  Maximum speech duration in seconds (default: 8.0)\n";
  std::cout << "\n";
  std::cout << "  --sense-voice-model <path> Path to SenseVoice model file (required)\n";
  std::cout << "  --tokens <path>           Path to tokens.txt file (required)\n";
  std::cout << "  --num-threads <num>       Number of threads for ASR computation (default: 2)\n";
  std::cout << "  --use-itn <0|1>           Use Inverse Text Normalization (default: 1)\n";
  std::cout << "\n";
  std::cout << "  --vad-window-size-ms <ms> VAD window size in milliseconds (default: 30)\n";
  std::cout << "  --update-interval-ms <ms> Result update interval in milliseconds (default: 200)\n";
  std::cout << "  --max-batch-size <num>    Maximum batch size for processing (default: 5)\n";
  std::cout << "\n";
  std::cout << "  --log-file <path>         Path to log file (empty for stdout)\n";
  std::cout << "  --data-dir <path>         Directory to save audio and recognition results\n";
  std::cout << "\n";
  std::cout << "  --connection-timeout <s>  Connection timeout in seconds (default: 15)\n";
  std::cout << "  --recognition-timeout <s> Recognition timeout in seconds (default: 30)\n";
  std::cout << "\n";
  std::cout << "  --help                    Show this help message\n";
  std::cout << "\n";
  std::cout << "Example:\n";
  std::cout << "  " << program_name << " \\\n";
  std::cout << "    --silero-vad-model /models/k2-fsa/silero_vad.onnx \\\n";
  std::cout << "    --sense-voice-model /models/k2-fsa/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/model.int8.onnx \\\n";
  std::cout << "    --tokens /models/k2-fsa/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17/tokens.txt \\\n";
  std::cout << "    --port 2026 \\\n";
  std::cout << "    --max-connections 256 \\\n";
  std::cout << "    --log-file /var/log/zasr.log\n";
}

// 主函数
int main(int argc, char* argv[]) {
  // 注册信号处理
  RegisterSignalHandlers();

  // 解析命令行参数
  ZAsrConfig config;

  // 读取参数
  if (!config.FromCommandLine(argc, argv)) {
    PrintUsage(argv[0]);
    return 1;
  }

  // 验证配置
  if (!config.Validate()) {
    std::cerr << "Configuration validation failed." << std::endl;
    PrintUsage(argv[0]);
    return 1;
  }
  
  try {
    // 创建服务器
    g_server = std::make_unique<ZAsrServer>(config);
    
    // 启动服务器
    if (!g_server->Start()) {
      std::cerr << "Failed to start server." << std::endl;
      return 1;
    }
    
    // 服务器运行中...
    std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;
    
    // 等待服务器停止
    // 注意：Start() 函数是阻塞的，服务器会在其中运行
    // 当收到停止信号时，SignalHandler 会调用 server->Stop()
    
    return 0;
    
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
