#include "zasr-connection.h"
#include "zasr-config.h"
#include "zasr-server.h"
#include "zasr-logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <filesystem>
#include <random>
#include <chrono>
#include "json.hpp"

using json = nlohmann::json;

namespace zasr {

// ClientConfig 实现
void ClientConfig::FromJson(const json& j) {
  format = j.value("fmt", "pcm");
  sample_rate = j.value("rate", 16000);
  enable_inverse_text_normalization = j.value("itn", true);
  max_sentence_silence = j.value("silence", 800);
}

std::string ClientConfig::ToString() const {
  std::ostringstream oss;
  oss << "ClientConfig{"
      << "format=" << format
      << ", rate=" << sample_rate
      << ", enable_itn=" << enable_inverse_text_normalization
      << ", silence=" << max_sentence_silence << "ms"
      << "}";
  return oss.str();
}

// 工具函数：生成UUID（简单实现）
std::string generate_uuid() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);
  static std::uniform_int_distribution<> dis2(8, 11);
  
  std::stringstream ss;
  
  for (int i = 0; i < 32; i++) {
    if (i == 8 || i == 12 || i == 16 || i == 20) {
      ss << "-";
    }
    
    int n;
    if (i == 12) {
      n = 4;  // Version 4
    } else if (i == 16) {
      n = dis2(gen);  // Variant
    } else {
      n = dis(gen);
    }
    
    ss << std::hex << n;
  }
  
  return ss.str();
}

// ZAsrConnection 实现
ZAsrConnection::ZAsrConnection(ZAsrServer* server, connection_hdl hdl)
    : server_(server), hdl_(hdl) {
  // 初始化最后活动时间
  UpdateActivityTime();
  last_update_time_ = std::chrono::steady_clock::now();

  // 初始化数据目录
  if (server_) {
    const auto& config = server_->GetConfig();
    data_dir_ = config.data_dir;
    
    // 如果数据目录为空，使用临时目录
    if (data_dir_.empty()) {
      const char* tmp_dir = std::getenv("TMPDIR");
      if (!tmp_dir) tmp_dir = std::getenv("TEMP");
      if (!tmp_dir) tmp_dir = "/tmp";
      data_dir_ = std::string(tmp_dir) + "/zasr";
    }
  }
}

ZAsrConnection::~ZAsrConnection() {
  StopProcessing();
}

void ZAsrConnection::HandleTextMessage(const std::string& message) {
  LOG_DEBUG() << "HandleTextMessage: received message, state=" << static_cast<int>(state_)
            << ", session_id=" << session_id_;
  UpdateActivityTime();
  
  try {
    HandleProtocolMessage(message);
  } catch (const json::parse_error& e) {
    SendError(ErrorCode::ERR_INVALID_JSON_FORMAT, "Invalid JSON format: " + std::string(e.what()));
  } catch (const std::exception& e) {
    SendError(ErrorCode::ERR_ERROR_PROCESSING_MESSAGE, "Error processing message: " + std::string(e.what()));
  }
}

void ZAsrConnection::HandleProtocolMessage(const std::string& json_str) {
  try {
    json j = json::parse(json_str);

    if (!j.contains("header") || !j["header"].is_object()) {
      SendError(ErrorCode::ERR_MISSING_OR_INVALID_HEADER, "Missing or invalid header");
      return;
    }

    json header = j["header"];
    if (!header.contains("name")) {
      SendError(ErrorCode::ERR_MISSING_NAME_IN_HEADER, "Missing name in header");
      return;
    }

    std::string name = header["name"];

    json payload = j.value("payload", json::object());

    if (name == "Begin") {
      HandleStartTranscription(header, payload);
    } else if (name == "End") {
      HandleStopTranscription(header, payload);
    } else {
      SendError(ErrorCode::ERR_UNSUPPORTED_MESSAGE_NAME, "Unsupported message name: " + name);
    }
    
  } catch (const json::parse_error& e) {
    SendError(ErrorCode::ERR_INVALID_JSON_FORMAT, "Invalid JSON format: " + std::string(e.what()));
  } catch (const std::exception& e) {
    SendError(ErrorCode::ERR_ERROR_PROCESSING_PROTOCOL_MESSAGE, "Error processing protocol message: " + std::string(e.what()));
  }
}

void ZAsrConnection::HandleStartTranscription(const json& header, const json& payload) {
  LOG_DEBUG() << "HandleStartTranscription: called, state=" << static_cast<int>(state_)
            << ", session_id=" << session_id_;
  if (state_ != ConnectionState::CONNECTED) {
    SendError(ErrorCode::ERR_INVALID_STATE_FOR_START_TRANSCRIPTION, "Invalid state for StartTranscription");
    return;
  }

  try {
    // 解析客户端配置
    client_config_.FromJson(payload);
    
    // 验证音频格式
    if (client_config_.format != "pcm") {
      SendError(ErrorCode::ERR_UNSUPPORTED_AUDIO_FORMAT, "Unsupported audio format: " + client_config_.format);
      return;
    }

    // 验证采样率
    if (client_config_.sample_rate != 16000) {
      SendError(ErrorCode::ERR_UNSUPPORTED_SAMPLE_RATE, "Unsupported sample rate: " + std::to_string(client_config_.sample_rate) + "Hz");
      return;
    }
    
    // 从服务器获取配置并初始化VAD和ASR
    if (server_) {
      const auto& config = server_->GetConfig();

      // 初始化VAD
      sherpa_onnx::cxx::VadModelConfig vad_config;
      vad_config.silero_vad.model = config.silero_vad_model;

      // 使用客户端配置的VAD参数 TODO 根据客户端配置仔细调节这些参数
      // 当环境嘈杂时可降低这个参数
      vad_config.silero_vad.threshold = config.vad_threshold;
      if (client_config_.max_sentence_silence > 50) {
          vad_config.silero_vad.min_silence_duration = client_config_.max_sentence_silence / 1000.0f;
      }
      else {
          vad_config.silero_vad.min_silence_duration = config.min_silence_duration;
      }
      // 有效语音最短持续时间 250ms，当环境嘈杂时可提高这个参数
      vad_config.silero_vad.min_speech_duration = config.min_speech_duration; // 0.25f
      vad_config.silero_vad.max_speech_duration = config.max_speech_duration;
      vad_config.sample_rate = config.sample_rate;

      // 计算VAD窗口大小（样本数）
      vad_window_size_ = static_cast<int32_t>(
          config.sample_rate * config.vad_window_size_ms / 1000.0f);

      // 创建VAD实例
      vad_ = std::make_unique<sherpa_onnx::cxx::VoiceActivityDetector>(
          sherpa_onnx::cxx::VoiceActivityDetector::Create(vad_config, 100.0f));  // 100秒缓冲区

      // 初始化ASR
      sherpa_onnx::cxx::OfflineRecognizerConfig asr_config;
      asr_config.model_config.model_type = "sense_voice";
      asr_config.model_config.sense_voice.model = config.sense_voice_model;
      asr_config.model_config.sense_voice.use_itn = client_config_.enable_inverse_text_normalization;
      asr_config.model_config.debug = false;
      asr_config.model_config.num_threads = config.num_threads;
      asr_config.model_config.provider = "cpu";  // 使用CPU

      // 设置tokens路径
      asr_config.model_config.tokens = config.tokens_path;

      // 创建ASR识别器
      recognizer_ = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(
          sherpa_onnx::cxx::OfflineRecognizer::Create(asr_config));

      LOG_INFO() << "VAD and ASR initialized for connection with config: "
                << client_config_.ToString();
    } else {
      SendError(ErrorCode::ERR_SERVER_CONFIG_NOT_AVAILABLE, "Server configuration not available");
      return;
    }
    
    // 生成session_id（如果客户端提供了就使用，否则生成新的）
    session_id_ = payload.value("session_id", "");
    if (session_id_.empty()) {
      session_id_ = generate_uuid();
    }
    
    // 发送Started响应
    json response_payload;
    response_payload["sid"] = session_id_;

    SendProtocolMessage("Started", response_payload);
    
    // 更新状态
    state_ = ConnectionState::STARTED;

    LOG_INFO() << "Transcription started: session_id=" << session_id_;

  } catch (const std::exception& e) {
    SendError(ErrorCode::ERR_ERROR_PROCESSING_START_TRANSCRIPTION, "Error processing StartTranscription: " + std::string(e.what()));
  }
}

void ZAsrConnection::HandleStopTranscription(const json& header, const json& payload) {
  LOG_DEBUG() << "HandleStopTranscription: called, state=" << static_cast<int>(state_)
            << ", session_id=" << session_id_;
  if (state_ == ConnectionState::CONNECTED) {
    SendError(ErrorCode::ERR_TRANSCRIPTION_NOT_STARTED, "Transcription not started");
    return;
  }
  
  // 处理剩余的音频数据
  if (state_ == ConnectionState::PROCESSING) {
    std::lock_guard<std::recursive_mutex> lock(buffer_mutex_);
    if (!audio_buffer_.empty()) {
      ProcessAudioBuffer();
    }
  }
  
  // 如果有活跃的句子，发送结束事件
  if (current_sentence_.active) {
    SendSentenceEnd(current_sentence_.index,
                   SamplesToMs(total_samples_),
                   current_sentence_.begin_time,
                   current_sentence_.result);
  }
  
  // 发送完成事件
  SendTranscriptionCompleted();

  // 关闭连接
  Close();

  // 主动关闭WebSocket连接
  if (server_) {
    try {
      server_->Close(hdl_, websocketpp::close::status::normal, "Transcription completed");
    } catch (const std::exception& e) {
      LOG_ERROR() << "HandleStopTranscription: Failed to close WebSocket: " << e.what();
    }
  }
}

void ZAsrConnection::HandleBinaryMessage(const void* data, size_t length) {
  // 调试日志
  LOG_DEBUG() << "HandleBinaryMessage: Received " << length
            << " bytes, state=" << static_cast<int>(state_);
  
  UpdateActivityTime();
  
  // 检查连接是否仍然活跃
  if (!is_active_) {
    LOG_DEBUG() << "HandleBinaryMessage: Connection not active, ignoring binary message";
    return;
  }
  
  
  if (state_ != ConnectionState::STARTED &&
      state_ != ConnectionState::PROCESSING) {
    SendError(ErrorCode::ERR_TRANSCRIPTION_NOT_STARTED_OR_WRONG_STATE, "Transcription not started or wrong state");
    return;
  }
  
  // 计算样本数（每个样本2字节，s16le）
  size_t num_samples = length / 2;
  if (num_samples == 0) {
  // 调试：显示样本数
    LOG_DEBUG() << "HandleBinaryMessage: " << num_samples << " samples";

    return;
  }
  
  // 转换为int16_t数组
  const int16_t* samples = static_cast<const int16_t*>(data);
  std::vector<int16_t> new_samples(samples, samples + num_samples);
  
  // 添加到缓冲区
  {
    std::lock_guard<std::recursive_mutex> lock(buffer_mutex_);
    audio_buffer_.insert(audio_buffer_.end(), new_samples.begin(), new_samples.end());
    total_samples_ += num_samples;
    total_ms_ = SamplesToMs(total_samples_);
  }
  // 调试：显示缓冲区大小
  LOG_DEBUG() << "HandleBinaryMessage: Buffer size now " << audio_buffer_.size()
            << " samples, total_samples_=" << total_samples_;
  
  
  // 如果还没开始处理，启动处理
  if (state_ == ConnectionState::STARTED) {
    state_ = ConnectionState::PROCESSING;
  }
  
  // 处理音频数据
  ProcessAudioBuffer();
}

void ZAsrConnection::StartProcessing() {
  // 已在HandleStartTranscription中初始化
}

void ZAsrConnection::StopProcessing() {
  is_active_ = false;

  // 清理资源
  {
    std::lock_guard<std::recursive_mutex> lock(buffer_mutex_);
    audio_buffer_.clear();
    float_buffer_.clear();
  }
  
  vad_.reset();
  recognizer_.reset();
  current_stream_.reset();
}

void ZAsrConnection::ProcessAudioBuffer() {
  std::lock_guard<std::recursive_mutex> lock(buffer_mutex_);
  
  // 检查连接是否仍然活跃
  if (!is_active_) {
    LOG_DEBUG() << "ProcessAudioBuffer: Connection not active, skipping";
    return;
  }
  

  // 调试日志
  LOG_DEBUG() << "ProcessAudioBuffer called. audio_buffer_.size()="
            << audio_buffer_.size() << ", vad_=" << (vad_ ? "not null" : "null")
            << ", recognizer_=" << (recognizer_ ? "not null" : "null");

  if (audio_buffer_.empty() || !vad_ || !recognizer_) {
    LOG_DEBUG() << "ProcessAudioBuffer: Skipping - empty buffer or null vad/recognizer";
    return;
  }
  
  // 转换为float
  std::vector<float> float_samples = Int16ToFloat(audio_buffer_);
  LOG_DEBUG() << "ProcessAudioBuffer: Converted to float, size=" << float_samples.size()
            << ", vad_offset_=" << vad_offset_ << ", vad_window_size_=" << vad_window_size_;
  
  // 处理VAD窗口 - 修复条件：使用 <= 而不是 <
  while (vad_offset_ + vad_window_size_ <= float_samples.size()) {
    LOG_DEBUG() << "ProcessAudioBuffer: Processing VAD window at offset="
              << vad_offset_;
    
    if (vad_offset_ + vad_window_size_ > float_samples.size()) {
      LOG_ERROR() << "ProcessAudioBuffer: Invalid VAD window access! vad_offset_="
                << vad_offset_ << ", window_size=" << vad_window_size_
                << ", float_samples.size()=" << float_samples.size();
      break;
    }
    
    vad_->AcceptWaveform(float_samples.data() + vad_offset_, vad_window_size_);

    if (!speech_started_ && vad_->IsDetected()) {
      speech_started_ = true;
      streamed_offset_ = 0;
      speech_start_time_ = std::chrono::steady_clock::now();

      // 创建新的识别流
      current_stream_ = std::make_unique<sherpa_onnx::cxx::OfflineStream>(
          recognizer_->CreateStream());
      LOG_DEBUG() << "ProcessAudioBuffer: Speech detected, created stream";
      
      // 开始新句子
      sentence_counter_++;
      current_sentence_.index = sentence_counter_;
      current_sentence_.begin_time = total_ms_;
      current_sentence_.current_time = total_ms_;
      current_sentence_.result = "";
      current_sentence_.active = true;
      
      // 发送句子开始事件
      SendSentenceBegin(sentence_counter_, total_ms_);
      LOG_DEBUG() << "ProcessAudioBuffer: Sent SentenceBegin for sentence "
                << sentence_counter_;
    }

    vad_offset_ += vad_window_size_;
    LOG_DEBUG() << "ProcessAudioBuffer: Updated vad_offset_ to " << vad_offset_;
  }
  
  // 如果没有检测到语音，清理缓冲区 - 修复计算错误
  if (!speech_started_) {
    LOG_DEBUG() << "ProcessAudioBuffer: No speech detected, checking buffer cleanup";
    if (float_samples.size() > 10 * vad_window_size_) {
      size_t new_size = 10 * vad_window_size_;
      size_t samples_to_remove = float_samples.size() - new_size;
      
      // 修复：确保vad_offset_不会变成负数
            // 同样调整streamed_offset_
            if (streamed_offset_ > samples_to_remove) {
              streamed_offset_ -= samples_to_remove;
            } else {
              streamed_offset_ = 0;
            }
      if (vad_offset_ > samples_to_remove) {
        vad_offset_ -= samples_to_remove;
      } else {
        vad_offset_ = 0;
      }

      LOG_DEBUG() << "ProcessAudioBuffer: Trimming buffer, old_size="
                << float_samples.size() << ", new_size=" << new_size
                << ", vad_offset_ now=" << vad_offset_;
      
      float_samples = std::vector<float>(
          float_samples.end() - new_size, 
          float_samples.end());
          
      // 修复：audio_buffer_和float_samples元素数量相同，不需要*2
      audio_buffer_ = std::vector<int16_t>(
          audio_buffer_.end() - new_size, 
          audio_buffer_.end());
    }
  }
  
  // 如果检测到语音，接受波形到识别器
  if (speech_started_ && current_stream_) {
    LOG_DEBUG() << "ProcessAudioBuffer: Speech started, feeding to recognizer, float_samples.size()="
              << float_samples.size();
    
    if (!float_samples.empty()) {
            // 计算需要送入的新样本数量
            size_t new_samples = 0;
            if (streamed_offset_ <= float_samples.size()) {
              new_samples = float_samples.size() - streamed_offset_;
            } else {
              // streamed_offset_ 超出范围，重置并送入所有样本
              streamed_offset_ = 0;
              new_samples = float_samples.size();
            }
            
            if (new_samples > 0) {
              LOG_DEBUG() << "ProcessAudioBuffer: Feeding " << new_samples << " new samples to recognizer, streamed_offset_=" << streamed_offset_;
              current_stream_->AcceptWaveform(16000, float_samples.data() + streamed_offset_, new_samples);
              streamed_offset_ += new_samples;
            }
      
      // 检查是否需要更新结果（每200ms）
      auto now = std::chrono::steady_clock::now();
      auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - last_update_time_).count();
      
      if (elapsed_ms > 200) {  // 200ms更新间隔
        LOG_DEBUG() << "ProcessAudioBuffer: Updating recognition result";
        recognizer_->Decode(current_stream_.get());
        auto result = recognizer_->GetResult(current_stream_.get());

        // 更新当前句子结果
        current_sentence_.result = result.text;
        current_sentence_.current_time = total_ms_;

        LOG_INFO() << "ProcessAudioBuffer: Recognition result: "
                  << current_sentence_.result;

        // 发送识别结果变更事件
        SendTranscriptionResultChanged(current_sentence_.index,
                                      total_ms_,
                                      current_sentence_.result);

        last_update_time_ = now;
      }
    }
  }

  // 检查VAD是否检测到语音片段结束 - 添加安全检查
  int pop_count = 0;
  while (vad_ && !vad_->IsEmpty()) {
    vad_->Pop();
    pop_count++;
  }

  if (pop_count > 0) {
    LOG_DEBUG() << "ProcessAudioBuffer: VAD popped " << pop_count << " results";

    // 语音片段结束，发送最终结果
    if (current_stream_) {
      LOG_DEBUG() << "ProcessAudioBuffer: Speech segment ended, getting final result";
      recognizer_->Decode(current_stream_.get());
      auto result = recognizer_->GetResult(current_stream_.get());

      current_sentence_.result = result.text;
      current_sentence_.current_time = total_ms_;

      LOG_DEBUG() << "ProcessAudioBuffer: Final result: "
                << current_sentence_.result;
      
      // 发送句子结束事件
      SendSentenceEnd(current_sentence_.index,
                     total_ms_,
                     current_sentence_.begin_time,
                     current_sentence_.result);
      
      // 重置状态
      speech_started_ = false;
      streamed_offset_ = 0;
      current_stream_.reset();
      current_sentence_.active = false;
      LOG_DEBUG() << "ProcessAudioBuffer: Reset speech state";
    }

    // 清空缓冲区
    audio_buffer_.clear();
    float_buffer_.clear();
    vad_offset_ = 0;
    LOG_DEBUG() << "ProcessAudioBuffer: Cleared buffers";
  }

  LOG_DEBUG() << "ProcessAudioBuffer finished";
}
std::vector<float> ZAsrConnection::Int16ToFloat(const std::vector<int16_t>& int16_samples) {
  std::vector<float> float_samples;
  float_samples.reserve(int16_samples.size());
  
  for (int16_t sample : int16_samples) {
    // 将int16转换为float，范围[-1.0, 1.0]
    float_samples.push_back(sample / 32768.0f);
  }
  
  return float_samples;
}

int64_t ZAsrConnection::SamplesToMs(int64_t samples) const {
  // 16000Hz采样率，每毫秒16个样本
  return samples / 16;
}

void ZAsrConnection::SendIntermediateResult() {
  if (current_sentence_.active && !current_sentence_.result.empty()) {
    SendTranscriptionResultChanged(current_sentence_.index,
                                  current_sentence_.current_time,
                                  current_sentence_.result);
  }
}

void ZAsrConnection::SendProtocolMessage(const std::string& name,
                                         const json& payload,
                                         int status,
                                         const std::string& status_text) {
  json header;
  header["name"] = name;
  header["status"] = status;
  header["mid"] = GenerateMessageId();
  header["status_text"] = status_text;

  json message;
  message["header"] = header;
  message["payload"] = payload;

  SendMessage(message.dump());
}

void ZAsrConnection::SendSentenceBegin(int index, int time_ms) {
  json payload;
  payload["idx"] = index;
  payload["time"] = time_ms;

  SendProtocolMessage("SentenceBegin", payload);
}

void ZAsrConnection::SendTranscriptionResultChanged(int index, int time_ms,
                                                    const std::string& result) {
  json payload;
  payload["idx"] = index;
  payload["time"] = time_ms;
  payload["text"] = result;

  SendProtocolMessage("Result", payload);
}

void ZAsrConnection::SendSentenceEnd(int index, int time_ms, int begin_time,
                                     const std::string& result) {
  json payload;
  payload["idx"] = index;
  payload["time"] = time_ms;
  payload["begin"] = begin_time;
  payload["text"] = result;

  SendProtocolMessage("SentenceEnd", payload);
}

void ZAsrConnection::SendTranscriptionCompleted() {
  SendProtocolMessage("Completed", json::object());
}

void ZAsrConnection::SendError(int status, const std::string& status_text) {
  SendProtocolMessage("Failed", json::object(), status, status_text);
}

void ZAsrConnection::SendMessage(const std::string& message) {
  // 通过服务器发送消息
  if (server_) {
    server_->SendMessage(hdl_, message);
  }
}

void ZAsrConnection::Close() {
  // 如果已经关闭，直接返回
  if (state_ == ConnectionState::CLOSING || state_ == ConnectionState::CLOSED) {
    return;
  }

  LOG_DEBUG() << "ZAsrConnection::Close: Closing connection, session_id="
            << session_id_;
  
  state_ = ConnectionState::CLOSING;
  is_active_ = false;

  // 清理音频缓冲区
  {
    std::lock_guard<std::recursive_mutex> lock(buffer_mutex_);
    audio_buffer_.clear();
    float_buffer_.clear();
    vad_offset_ = 0;
  }

  // 如果有活跃的句子，发送结束事件
  if (current_sentence_.active) {
    LOG_DEBUG() << "ZAsrConnection::Close: Sending final SentenceEnd for active sentence";
    try {
      SendSentenceEnd(current_sentence_.index,
                     total_ms_,
                     current_sentence_.begin_time,
                     current_sentence_.result);
    } catch (const std::exception& e) {
      LOG_ERROR() << "ZAsrConnection::Close: Failed to send SentenceEnd: " << e.what();
    }
  }

  // 发送转录完成事件（如果已经开始转录）
  if (state_ != ConnectionState::CONNECTED) {
    LOG_DEBUG() << "ZAsrConnection::Close: Sending TranscriptionCompleted";
    try {
      SendTranscriptionCompleted();
    } catch (const std::exception& e) {
      LOG_ERROR() << "ZAsrConnection::Close: Failed to send TranscriptionCompleted: " << e.what();
    }
  }

  // 清理资源
  vad_.reset();
  recognizer_.reset();
  current_stream_.reset();
  
  // 更新状态
  state_ = ConnectionState::CLOSED;

  LOG_DEBUG() << "ZAsrConnection::Close: Connection closed successfully";
}

bool ZAsrConnection::IsTimeout(int timeout_seconds) const {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      now - last_activity_time_).count();
  return elapsed > timeout_seconds;
}

void ZAsrConnection::UpdateActivityTime() {
  last_activity_time_ = std::chrono::steady_clock::now();
}

std::string ZAsrConnection::GenerateMessageId() {
  return generate_uuid();
}

}  // namespace zasr
