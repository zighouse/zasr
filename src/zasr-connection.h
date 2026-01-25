#ifndef ZASR_CONNECTION_H
#define ZASR_CONNECTION_H

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <deque>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iomanip>

#include "websocketpp/config/asio_no_tls.hpp"
#include "websocketpp/server.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"
#include "json.hpp"

namespace zasr {

using server = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;

class ZAsrServer;

// 客户端配置（从StartTranscription的payload中获取）
struct ClientConfig {
  std::string format = "pcm";                    // 音频格式
  int sample_rate = 16000;                       // 采样率
  bool enable_inverse_text_normalization = true; // 是否启用ITN
  int max_sentence_silence = 500;                // 断句检测阈值（ms）

  // 从JSON解析
  void FromJson(const nlohmann::json& j);
  std::string ToString() const;
};

// 连接状态枚举
enum class ConnectionState {
  CONNECTED,      // 已连接，等待认证
  STARTED,        // 已开始转录
  PROCESSING,     // 正在处理音频
  CLOSING,        // 正在关闭
  CLOSED          // 已关闭
};

// 错误码定义
namespace ErrorCode {
  // 转录相关错误
  constexpr int ERR_INVALID_STATE_FOR_START_TRANSCRIPTION       = 1001;
  constexpr int ERR_UNSUPPORTED_AUDIO_FORMAT                    = 1002;
  constexpr int ERR_UNSUPPORTED_SAMPLE_RATE                     = 1003;
  constexpr int ERR_ERROR_PROCESSING_START_TRANSCRIPTION        = 1004;
  constexpr int ERR_TRANSCRIPTION_NOT_STARTED                   = 1005;
  constexpr int ERR_TRANSCRIPTION_NOT_STARTED_OR_WRONG_STATE    = 1006;

  // 协议/消息相关错误
  constexpr int ERR_INVALID_JSON_FORMAT                         = 2001;
  constexpr int ERR_ERROR_PROCESSING_MESSAGE                    = 2002;
  constexpr int ERR_MISSING_OR_INVALID_HEADER                   = 2003;
  constexpr int ERR_MISSING_NAME_IN_HEADER                      = 2004;
  constexpr int ERR_UNSUPPORTED_MESSAGE_NAME                    = 2005;
  constexpr int ERR_ERROR_PROCESSING_PROTOCOL_MESSAGE           = 2006;
  constexpr int ERR_SERVER_CONFIG_NOT_AVAILABLE                 = 2007;
}  // namespace ErrorCode

// 句子状态
struct SentenceState {
  int index = 0;            // 句子编号
  int64_t begin_time = 0;   // 句子开始时间（毫秒）
  int64_t current_time = 0; // 当前处理时间（毫秒）
  std::string result;       // 当前识别结果

  bool active = false;      // 是否活跃
};

// 连接数据类，管理单个客户端连接
class ZAsrConnection : public std::enable_shared_from_this<ZAsrConnection> {
 public:
  ZAsrConnection(ZAsrServer* server, connection_hdl hdl);
  ~ZAsrConnection();
  
  // 禁止拷贝和赋值
  ZAsrConnection(const ZAsrConnection&) = delete;
  ZAsrConnection& operator=(const ZAsrConnection&) = delete;
  
  // 获取连接句柄
  connection_hdl GetHandle() const { return hdl_; }
  
  // 处理文本消息（认证等）
  void HandleTextMessage(const std::string& message);
  
  // 处理二进制消息（音频数据）
  void HandleBinaryMessage(const void* data, size_t length);
  
  // 开始处理（从工作线程调用）
  void StartProcessing();
  
  // 停止处理
  void StopProcessing();
  
  // 检查是否活跃
  bool IsActive() const { return is_active_; }
  
  // 获取最后活动时间
  std::chrono::steady_clock::time_point GetLastActivityTime() const { 
    return last_activity_time_; 
  }
  
  // 检查是否超时
  bool IsTimeout(int timeout_seconds) const;
  
  // 发送消息到客户端
  void SendMessage(const std::string& message);
  
  // 发送协议消息
  void SendProtocolMessage(const std::string& name, 
                          const nlohmann::json& payload,
                          int status = 20000000,
                          const std::string& status_text = "Gateway:SUCCESS:Success.");
  
  // 发送句子开始事件
  void SendSentenceBegin(int index, int time_ms);
  
  // 发送识别结果变更事件
  void SendTranscriptionResultChanged(int index, int time_ms,
                                      const std::string& result);

  // 发送句子结束事件
  void SendSentenceEnd(int index, int time_ms, int begin_time,
                       const std::string& result);
  
  // 发送转录完成事件
  void SendTranscriptionCompleted();
  
  // 发送错误消息
  void SendError(int status, const std::string& status_text);
  
  // 关闭连接
  void Close();
  
 private:
  // 日志方法
  void LogDebug(const std::string& message);
  void LogError(const std::string& message);
  void LogInfo(const std::string& message);
  
  // 处理消息
  void HandleProtocolMessage(const std::string& json_str);
  
  // 处理StartTranscription消息
  void HandleStartTranscription(const nlohmann::json& header, const nlohmann::json& payload);
  
  // 处理StopTranscription消息
  void HandleStopTranscription(const nlohmann::json& header, const nlohmann::json& payload);
  
  // 处理音频数据
  void ProcessAudioData(const std::vector<int16_t>& samples);

  // 执行VAD和ASR处理
  void ProcessAudioBuffer();

  // 离线模式处理（SenseVoice + VAD）
  void ProcessOfflineMode();

  // 在线模式处理（Streaming Zipformer）
  void ProcessOnlineMode();
  
  // 发送中间结果
  void SendIntermediateResult();
  
  // 转换int16到float
  std::vector<float> Int16ToFloat(const std::vector<int16_t>& int16_samples);
  
  // 转换样本数为毫秒
  int64_t SamplesToMs(int64_t samples) const;

  // 添加标点符号
  std::string AddPunctuation(const std::string& text);

  // 更新最后活动时间
  void UpdateActivityTime();
  
  // 生成消息ID
  std::string GenerateMessageId();
  
 private:
  ZAsrServer* server_;  // 指向服务器的指针（不拥有）
  connection_hdl hdl_;   // WebSocket连接句柄
  
  // 连接状态
  ConnectionState state_ = ConnectionState::CONNECTED;
  std::atomic<bool> is_active_{true};
  
  // 会话信息
  std::string session_id_;
  
  // 客户端配置（从StartTranscription获取）
  ClientConfig client_config_;
  
  // 音频处理
  std::vector<int16_t> audio_buffer_;  // 原始int16音频数据
  std::vector<float> float_buffer_;    // 转换为float的音频数据
  int64_t total_samples_ = 0;          // 总样本数
  int64_t total_ms_ = 0;               // 总毫秒数
  
  // VAD相关
  std::unique_ptr<sherpa_onnx::cxx::VoiceActivityDetector> vad_;
  int32_t vad_window_size_;           // VAD窗口大小（样本数）
  int32_t vad_offset_ = 0;            // VAD缓冲区偏移
  bool speech_started_ = false;       // 是否开始检测到语音
  std::chrono::steady_clock::time_point speech_start_time_;  // 语音开始时间

  // ASR相关
  std::unique_ptr<sherpa_onnx::cxx::OfflineRecognizer> offline_recognizer_;
  std::unique_ptr<sherpa_onnx::cxx::OnlineRecognizer> online_recognizer_;
  std::unique_ptr<sherpa_onnx::cxx::OfflineStream> offline_stream_;  // 当前语音片段的离线流
  std::unique_ptr<sherpa_onnx::cxx::OnlineStream> online_stream_;    // 当前语音片段的在线流
  int32_t streamed_offset_ = 0;      // 已经送入识别器流的样本偏移量（float样本）
  bool use_online_recognizer_ = false;  // 是否使用在线识别器

  // 标点符号相关
  std::unique_ptr<sherpa_onnx::cxx::OfflinePunctuation> punctuation_;
  
  // 句子状态管理
  SentenceState current_sentence_;
  int sentence_counter_ = 0;      // 句子计数器
  
  // 时间管理
  std::chrono::steady_clock::time_point last_activity_time_;
  std::chrono::steady_clock::time_point last_update_time_;
  
  // 线程安全
  mutable std::recursive_mutex buffer_mutex_;   // 保护音频缓冲区（递归锁，避免 ProcessAudioBuffer 重入死锁）
  mutable std::mutex state_mutex_;    // 保护状态
  
  // 数据保存
  std::string data_dir_;              // 数据保存目录
  int32_t file_counter_ = 0;          // 文件计数器
};

}  // namespace zasr

#endif  // ZASR_CONNECTION_H
