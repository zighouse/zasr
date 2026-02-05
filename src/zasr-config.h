/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#ifndef ZASR_CONFIG_H
#define ZASR_CONFIG_H

#include <string>
#include <vector>
#include <cstring>

namespace zasr {

// 识别器类型枚举
enum class RecognizerType {
  kSenseVoice,           // SenseVoice (模拟流式，使用 OfflineRecognizer + VAD)
  kStreamingZipformer,   // Streaming Zipformer (真正流式，使用 OnlineRecognizer)
  kStreamingParaformer   // Streaming Paraformer (真正流式，使用 OnlineRecognizer)
};

// 辅助函数：获取默认模型路径
inline std::string GetDefaultModelPath(const std::string& filename) {
  const char* home = std::getenv("HOME");
  if (home) {
    return std::string(home) + "/.cache/sherpa-onnx/" + filename;
  }
  return "/models/sherpa-onnx/" + filename;
}

struct ZAsrConfig {
  // Server configuration
  std::string host = "0.0.0.0";
  int port = 2026;
  int max_connections = 8;
  int worker_threads = 4;

  // Audio configuration
  int sample_rate = 16000;
  int sample_width = 2;  // s16le = 2 bytes

  // VAD configuration
  std::string silero_vad_model;
  // 语音存在概率，Aurora-4 数据集上用 ts=0.5，ROC-AUC 达 0.98
  float vad_threshold = 0.5;
  float min_silence_duration = 0.1;  // seconds
  float min_speech_duration = 0.25;  // seconds
  float max_speech_duration = 8.0;   // seconds

  // ASR configuration
  RecognizerType recognizer_type = RecognizerType::kSenseVoice;

  // SenseVoice configuration (OfflineRecognizer)
  std::string sense_voice_model;
  std::string tokens_path;
  bool use_itn = true;
  int num_threads = 2;

  // Streaming Zipformer configuration (OnlineRecognizer)
  std::string zipformer_encoder;
  std::string zipformer_decoder;
  std::string zipformer_joiner;

  // Streaming Paraformer configuration (OnlineRecognizer)
  std::string paraformer_encoder;
  std::string paraformer_decoder;

  // Punctuation configuration
  bool enable_punctuation = false;
  std::string punctuation_model;
  
  // Processing configuration
  float vad_window_size_ms = 30;  // VAD窗口大小（毫秒）
  float update_interval_ms = 200; // 更新间隔（毫秒）
  int max_batch_size = 2;
  
  // Logging and storage
  std::string log_file;
  std::string data_dir;  // 保存音频和识别结果的目录
  
  // Timeouts
  int connection_timeout_seconds = 15;
  int recognition_timeout_seconds = 30;

  // Parse command line arguments
  // Returns true if parsing succeeded, false on error
  bool FromCommandLine(int argc, char* argv[]);

  // Load configuration from YAML file
  // Returns true if loading succeeded, false on error
  bool FromYamlFile(const std::string& filepath);

  bool Validate() const;
  std::string ToString() const;

private:
  // Helper function to find an argument by name (e.g., "--port")
  const char* FindArg(const std::vector<const char*>& args, const char* name) const;
  // Helper function to find an argument value by name (e.g., "--port")
  const char* FindArgValue(const std::vector<const char*>& args, const char* name) const;
};

}  // namespace zasr

#endif  // ZASR_CONFIG_H
