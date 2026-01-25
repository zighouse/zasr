#ifndef ZASR_CONFIG_H
#define ZASR_CONFIG_H

#include <string>
#include <vector>
#include <cstring>

namespace zasr {

struct ZAsrConfig {
  // Server configuration
  std::string host = "0.0.0.0";
  int port = 2026;
  int max_connections = 256;
  int worker_threads = 4;
  
  // Audio configuration
  int sample_rate = 16000;
  int sample_width = 2;  // s16le = 2 bytes
  
  // VAD configuration
  std::string silero_vad_model = "/models/k2-fsa/silero_vad.onnx";
  // 语音存在概率，Aurora-4 数据集上用 ts=0.5，ROC-AUC 达 0.98
  float vad_threshold = 0.5;
  float min_silence_duration = 0.1;  // seconds
  float min_speech_duration = 0.25;  // seconds
  float max_speech_duration = 8.0;   // seconds
  
  // ASR configuration
  std::string sense_voice_model;
  std::string tokens_path;
  bool use_itn = true;
  int num_threads = 2;
  
  // Processing configuration
  float vad_window_size_ms = 30;  // VAD窗口大小（毫秒）
  float update_interval_ms = 200; // 更新间隔（毫秒）
  int max_batch_size = 5;
  
  // Logging and storage
  std::string log_file;
  std::string data_dir;  // 保存音频和识别结果的目录
  
  // Timeouts
  int connection_timeout_seconds = 15;
  int recognition_timeout_seconds = 30;

  // Parse command line arguments
  // Returns true if parsing succeeded, false on error
  bool FromCommandLine(int argc, char* argv[]);
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
