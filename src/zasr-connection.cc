/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

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

      // 初始化ASR - 根据配置选择离线或在线识别器
      if (config.recognizer_type == RecognizerType::kSenseVoice) {
        // SenseVoice 模式：需要初始化 VAD
        LOG_INFO() << "Initializing SenseVoice recognizer...";
        LOG_INFO() << "VAD model path: " << config.silero_vad_model;
        LOG_INFO() << "SenseVoice model path: " << config.sense_voice_model;
        LOG_INFO() << "Tokens path: " << config.tokens_path;

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
        LOG_INFO() << "Creating VAD instance...";
        vad_ = std::make_unique<sherpa_onnx::cxx::VoiceActivityDetector>(
            sherpa_onnx::cxx::VoiceActivityDetector::Create(vad_config, 100.0f));  // 100秒缓冲区

        if (!vad_) {
          LOG_ERROR() << "Failed to create VAD instance";
          SendError(ErrorCode::ERR_ERROR_PROCESSING_START_TRANSCRIPTION, "Failed to create VAD instance");
          return;
        }
        LOG_INFO() << "VAD instance created successfully";

        // 使用 OfflineRecognizer (SenseVoice)
        sherpa_onnx::cxx::OfflineRecognizerConfig asr_config;
        asr_config.model_config.model_type = "sense_voice";
        asr_config.model_config.sense_voice.model = config.sense_voice_model;
        asr_config.model_config.sense_voice.use_itn = client_config_.enable_inverse_text_normalization;
        asr_config.model_config.debug = false;
        asr_config.model_config.num_threads = config.num_threads;
        asr_config.model_config.provider = "cpu";
        asr_config.model_config.tokens = config.tokens_path;

        LOG_INFO() << "Creating OfflineRecognizer instance...";
        offline_recognizer_ = std::make_unique<sherpa_onnx::cxx::OfflineRecognizer>(
            sherpa_onnx::cxx::OfflineRecognizer::Create(asr_config));

        if (!offline_recognizer_) {
          LOG_ERROR() << "Failed to create OfflineRecognizer";
          SendError(ErrorCode::ERR_ERROR_PROCESSING_START_TRANSCRIPTION, "Failed to create OfflineRecognizer");
          return;
        }
        LOG_INFO() << "OfflineRecognizer created successfully";
        use_online_recognizer_ = false;
      } else {
        // 使用 OnlineRecognizer (Streaming Zipformer 或 Streaming Paraformer)
        // 注意：streaming 识别器不需要 VAD，使用内置的端点检测
        sherpa_onnx::cxx::OnlineRecognizerConfig asr_config;
        asr_config.feat_config.sample_rate = config.sample_rate;
        asr_config.feat_config.feature_dim = 80;
        asr_config.model_config.tokens = config.tokens_path;
        asr_config.model_config.num_threads = config.num_threads;
        asr_config.model_config.provider = "cpu";
        asr_config.model_config.debug = false;

        if (config.recognizer_type == RecognizerType::kStreamingParaformer) {
          // Streaming Paraformer 配置
          LOG_INFO() << "Initializing Streaming Paraformer recognizer...";
          LOG_INFO() << "Encoder path: " << config.paraformer_encoder;
          LOG_INFO() << "Decoder path: " << config.paraformer_decoder;
          LOG_INFO() << "Tokens path: " << config.tokens_path;

          asr_config.model_config.paraformer.encoder = config.paraformer_encoder;
          asr_config.model_config.paraformer.decoder = config.paraformer_decoder;
          asr_config.model_config.model_type = "paraformer";
        } else {
          // Streaming Zipformer 配置
          LOG_INFO() << "Initializing Streaming Zipformer recognizer...";
          LOG_INFO() << "Encoder path: " << config.zipformer_encoder;
          LOG_INFO() << "Decoder path: " << config.zipformer_decoder;
          LOG_INFO() << "Joiner path: " << config.zipformer_joiner;
          LOG_INFO() << "Tokens path: " << config.tokens_path;

          asr_config.model_config.transducer.encoder = config.zipformer_encoder;
          asr_config.model_config.transducer.decoder = config.zipformer_decoder;
          asr_config.model_config.transducer.joiner = config.zipformer_joiner;
          asr_config.model_config.model_type = "transducer";
        }

        // 启用端点检测，实现自动断句
        asr_config.enable_endpoint = true;
        asr_config.rule1_min_trailing_silence = 1.2;  // 1.2秒静音后断句
        asr_config.rule2_min_trailing_silence = 0.8;
        asr_config.rule3_min_utterance_length = 10;   // 最短10ms的语音

        LOG_INFO() << "Creating OnlineRecognizer instance...";
        online_recognizer_ = std::make_unique<sherpa_onnx::cxx::OnlineRecognizer>(
            sherpa_onnx::cxx::OnlineRecognizer::Create(asr_config));

        if (!online_recognizer_) {
          SendError(ErrorCode::ERR_ERROR_PROCESSING_START_TRANSCRIPTION, "Failed to create OnlineRecognizer");
          return;
        }
        LOG_INFO() << "OnlineRecognizer created successfully";
        use_online_recognizer_ = true;
      }

      // 初始化标点符号模型（如果启用）
      if (config.enable_punctuation && !config.punctuation_model.empty()) {
        sherpa_onnx::cxx::OfflinePunctuationConfig punct_config;
        punct_config.model.ct_transformer = config.punctuation_model;
        punct_config.model.num_threads = config.num_threads;
        punct_config.model.provider = "cpu";
        punct_config.model.debug = false;

        punctuation_ = std::make_unique<sherpa_onnx::cxx::OfflinePunctuation>(
            sherpa_onnx::cxx::OfflinePunctuation::Create(punct_config));

        if (!punctuation_) {
          LOG_WARN() << "Failed to create OfflinePunctuation, continuing without punctuation";
        } else {
          LOG_INFO() << "Punctuation model initialized: " << config.punctuation_model;
        }
      }

      // Initialize speaker identification (if enabled)
      if (config.enable_speaker_identification) {
        LOG_INFO() << "Initializing speaker identification...";
        LOG_INFO() << "Speaker model: " << config.speaker_model;
        LOG_INFO() << "Voice print DB: " << config.voice_print_db;

        ZSpeakerIdentifier::Config sid_config;
        sid_config.model = config.speaker_model;
        sid_config.num_threads = config.num_threads;
        sid_config.debug = false;
        sid_config.provider = "cpu";
        sid_config.voice_print_db = config.voice_print_db;
        sid_config.similarity_threshold = config.speaker_similarity_threshold;
        sid_config.enable_auto_track = config.auto_track_new_speakers;

        speaker_identifier_ = std::make_unique<ZSpeakerIdentifier>(sid_config);

        if (!speaker_identifier_->Initialize()) {
          LOG_WARN() << "Failed to initialize SpeakerIdentifier, continuing without speaker identification";
          speaker_identifier_.reset();
          enable_speaker_identification_ = false;
        } else {
          enable_speaker_identification_ = true;
          LOG_INFO() << "Speaker identification initialized successfully";
        }
      } else {
        enable_speaker_identification_ = false;
      }

      LOG_INFO() << "ASR initialized for connection with config: "
                << client_config_.ToString()
                << ", recognizer_type: "
                << (use_online_recognizer_ ? "streaming-zipformer (no VAD)" : "sense-voice (with VAD)");
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
    current_sentence_audio_.clear();
  }

  vad_.reset();
  offline_recognizer_.reset();
  online_recognizer_.reset();
  offline_stream_.reset();
  online_stream_.reset();
  punctuation_.reset();
}

void ZAsrConnection::ProcessAudioBuffer() {
  std::lock_guard<std::recursive_mutex> lock(buffer_mutex_);

  // 检查连接是否仍然活跃
  if (!is_active_) {
    LOG_DEBUG() << "ProcessAudioBuffer: Connection not active, skipping";
    return;
  }

  // 检查识别器是否已初始化
  if ((use_online_recognizer_ && !online_recognizer_) ||
      (!use_online_recognizer_ && !offline_recognizer_)) {
    LOG_DEBUG() << "ProcessAudioBuffer: Skipping - recognizer not initialized";
    return;
  }

  if (audio_buffer_.empty()) {
    LOG_DEBUG() << "ProcessAudioBuffer: Skipping - empty buffer";
    return;
  }

  // 根据识别器类型采用不同的处理方式
  if (use_online_recognizer_) {
    ProcessOnlineMode();
  } else {
    ProcessOfflineMode();
  }
}

// 离线模式处理（SenseVoice + VAD）
void ZAsrConnection::ProcessOfflineMode() {
  // 调试日志
  LOG_DEBUG() << "ProcessOfflineMode: audio_buffer_.size()="
            << audio_buffer_.size() << ", vad_=" << (vad_ ? "not null" : "null");

  if (!vad_ || !offline_recognizer_) {
    LOG_DEBUG() << "ProcessOfflineMode: Skipping - null vad or recognizer";
    return;
  }

  // 转换为float
  std::vector<float> float_samples = Int16ToFloat(audio_buffer_);
  LOG_DEBUG() << "ProcessOfflineMode: Converted to float, size=" << float_samples.size()
            << ", vad_offset_=" << vad_offset_ << ", vad_window_size_=" << vad_window_size_;

  // 处理VAD窗口
  while (vad_offset_ + vad_window_size_ <= float_samples.size()) {
    LOG_DEBUG() << "ProcessOfflineMode: Processing VAD window at offset=" << vad_offset_;

    vad_->AcceptWaveform(float_samples.data() + vad_offset_, vad_window_size_);

    if (!speech_started_ && vad_->IsDetected()) {
      speech_started_ = true;
      streamed_offset_ = 0;
      speech_start_time_ = std::chrono::steady_clock::now();

      // 创建新的识别流
      offline_stream_ = std::make_unique<sherpa_onnx::cxx::OfflineStream>(
          offline_recognizer_->CreateStream());
      LOG_DEBUG() << "ProcessOfflineMode: Speech detected, created stream";

      // 开始新句子
      sentence_counter_++;
      current_sentence_.index = sentence_counter_;
      current_sentence_.begin_time = total_ms_;
      current_sentence_.current_time = total_ms_;
      current_sentence_.result = "";
      current_sentence_.active = true;

      // 发送句子开始事件
      SendSentenceBegin(sentence_counter_, total_ms_);
      LOG_DEBUG() << "ProcessOfflineMode: Sent SentenceBegin for sentence " << sentence_counter_;
    }

    vad_offset_ += vad_window_size_;
  }

  // 如果没有检测到语音，清理缓冲区
  if (!speech_started_) {
    LOG_DEBUG() << "ProcessOfflineMode: No speech detected, checking buffer cleanup";
    if (float_samples.size() > 10 * vad_window_size_) {
      size_t new_size = 10 * vad_window_size_;
      size_t samples_to_remove = float_samples.size() - new_size;

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

      LOG_DEBUG() << "ProcessOfflineMode: Trimming buffer, old_size="
                << float_samples.size() << ", new_size=" << new_size;

      float_samples = std::vector<float>(
          float_samples.end() - new_size,
          float_samples.end());

      audio_buffer_ = std::vector<int16_t>(
          audio_buffer_.end() - new_size,
          audio_buffer_.end());
    }
  }

  // 如果检测到语音，接受波形到识别器
  if (speech_started_ && offline_stream_) {
    LOG_DEBUG() << "ProcessOfflineMode: Feeding audio to recognizer";

    if (!float_samples.empty()) {
      // 计算需要送入的新样本数量
      size_t new_samples = 0;
      if (streamed_offset_ <= float_samples.size()) {
        new_samples = float_samples.size() - streamed_offset_;
      } else {
        streamed_offset_ = 0;
        new_samples = float_samples.size();
      }

      if (new_samples > 0) {
        LOG_DEBUG() << "ProcessOfflineMode: Feeding " << new_samples << " samples";
        offline_stream_->AcceptWaveform(16000, float_samples.data() + streamed_offset_, new_samples);
        streamed_offset_ += new_samples;
      }

      // 检查是否需要更新结果（每200ms）
      auto now = std::chrono::steady_clock::now();
      auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - last_update_time_).count();

      if (elapsed_ms > 200) {
        LOG_DEBUG() << "ProcessOfflineMode: Updating recognition result";
        offline_recognizer_->Decode(offline_stream_.get());
        auto result = offline_recognizer_->GetResult(offline_stream_.get());

        current_sentence_.result = result.text;
        current_sentence_.current_time = total_ms_;

        LOG_INFO() << "ProcessOfflineMode: Recognition result: " << current_sentence_.result;

        SendTranscriptionResultChanged(current_sentence_.index,
                                      total_ms_,
                                      current_sentence_.result);

        last_update_time_ = now;
      }
    }
  }

  // 检查VAD是否检测到语音片段结束
  int pop_count = 0;
  while (vad_ && !vad_->IsEmpty()) {
    vad_->Pop();
    pop_count++;
  }

  if (pop_count > 0) {
    LOG_DEBUG() << "ProcessOfflineMode: VAD popped " << pop_count << " results";

    // 语音片段结束，发送最终结果
    if (offline_stream_) {
      LOG_DEBUG() << "ProcessOfflineMode: Speech segment ended, getting final result";
      offline_recognizer_->Decode(offline_stream_.get());
      auto result = offline_recognizer_->GetResult(offline_stream_.get());

      current_sentence_.result = result.text;
      current_sentence_.current_time = total_ms_;

      LOG_DEBUG() << "ProcessOfflineMode: Final result: " << current_sentence_.result;

      // Perform speaker identification if enabled
      if (enable_speaker_identification_ && speaker_identifier_ && !audio_buffer_.empty()) {
        LOG_DEBUG() << "ProcessOfflineMode: Performing speaker identification";

        // Convert audio buffer to float for speaker identification
        std::vector<float> audio_segment = Int16ToFloat(audio_buffer_);

        // Identify speaker
        auto identification_result = speaker_identifier_->ProcessSegment(audio_segment);

        if (!identification_result.speaker_id.empty()) {
          current_speaker_id_ = identification_result.speaker_id;
          current_speaker_name_ = identification_result.speaker_name;

          LOG_INFO() << "ProcessOfflineMode: Identified speaker: "
                    << current_speaker_id_ << " (" << current_speaker_name_ << ")";

          if (identification_result.is_new_speaker) {
            LOG_INFO() << "ProcessOfflineMode: New speaker tracked automatically";
          }
        } else {
          // No speaker identified, clear previous speaker info
          current_speaker_id_.clear();
          current_speaker_name_.clear();
          LOG_DEBUG() << "ProcessOfflineMode: No speaker identified";
        }
      }

      SendSentenceEnd(current_sentence_.index,
                     total_ms_,
                     current_sentence_.begin_time,
                     current_sentence_.result);

      // 重置状态
      speech_started_ = false;
      streamed_offset_ = 0;
      offline_stream_.reset();
      current_sentence_.active = false;
      LOG_DEBUG() << "ProcessOfflineMode: Reset speech state";
    }

    // 清空缓冲区
    audio_buffer_.clear();
    float_buffer_.clear();
    vad_offset_ = 0;
    LOG_DEBUG() << "ProcessOfflineMode: Cleared buffers";
  }

  LOG_DEBUG() << "ProcessOfflineMode finished";
}

// 在线模式处理（Streaming Zipformer）
void ZAsrConnection::ProcessOnlineMode() {
  LOG_DEBUG() << "ProcessOnlineMode: audio_buffer_.size()=" << audio_buffer_.size();

  if (!online_recognizer_) {
    LOG_DEBUG() << "ProcessOnlineMode: Skipping - recognizer not initialized";
    return;
  }

  // 如果还没有创建流，创建第一个流
  if (!online_stream_) {
    online_stream_ = std::make_unique<sherpa_onnx::cxx::OnlineStream>(
        online_recognizer_->CreateStream());
    sentence_counter_++;
    current_sentence_.index = sentence_counter_;
    current_sentence_.begin_time = total_ms_;
    current_sentence_.current_time = total_ms_;
    current_sentence_.result = "";
    current_sentence_.active = true;

    SendSentenceBegin(sentence_counter_, total_ms_);
    LOG_DEBUG() << "ProcessOnlineMode: Created initial stream";
  }

  // 转换为float并送入识别器
  std::vector<float> float_samples = Int16ToFloat(audio_buffer_);

  if (!float_samples.empty()) {
    // 累积当前句子的音频数据用于说话人识别
    current_sentence_audio_.insert(current_sentence_audio_.end(),
                                   audio_buffer_.begin(),
                                   audio_buffer_.end());
    LOG_DEBUG() << "ProcessOnlineMode: Feeding " << float_samples.size() << " samples";
    online_stream_->AcceptWaveform(16000, float_samples.data(), float_samples.size());

    // 只有在有足够数据时才解码
    // 在线识别器需要积累一定数量的音频帧才能进行特征提取
    if (online_recognizer_->IsReady(online_stream_.get())) {
      // 解码并获取结果
      online_recognizer_->Decode(online_stream_.get());
      auto result = online_recognizer_->GetResult(online_stream_.get());

      // 如果结果有变化，发送更新
      if (result.text != current_sentence_.result) {
        current_sentence_.result = result.text;
        current_sentence_.current_time = total_ms_;

        LOG_INFO() << "ProcessOnlineMode: Recognition result: " << current_sentence_.result;

        SendTranscriptionResultChanged(current_sentence_.index,
                                      total_ms_,
                                      current_sentence_.result);
      }
    }

    // 检查是否到达端点（即使没有足够数据也可以检查）
    if (online_recognizer_->IsEndpoint(online_stream_.get())) {
      LOG_DEBUG() << "ProcessOnlineMode: Endpoint detected";

      // 获取最终结果
      auto final_result = online_recognizer_->GetResult(online_stream_.get());
      current_sentence_.result = final_result.text;
      current_sentence_.current_time = total_ms_;

      LOG_DEBUG() << "ProcessOnlineMode: Final result: " << current_sentence_.result;

      // Perform speaker identification if enabled (using accumulated sentence audio)
      if (enable_speaker_identification_ && speaker_identifier_ && !current_sentence_audio_.empty()) {
        LOG_DEBUG() << "ProcessOnlineMode: Performing speaker identification with "
                  << current_sentence_audio_.size() << " samples";

        // Convert accumulated audio to float for speaker identification
        std::vector<float> audio_segment = Int16ToFloat(current_sentence_audio_);

        // Identify speaker
        auto identification_result = speaker_identifier_->ProcessSegment(audio_segment);

        if (!identification_result.speaker_id.empty()) {
          current_speaker_id_ = identification_result.speaker_id;
          current_speaker_name_ = identification_result.speaker_name;

          LOG_INFO() << "ProcessOnlineMode: Identified speaker: "
                    << current_speaker_id_ << " (" << current_speaker_name_ << ")"
                    << " with confidence: " << identification_result.confidence;

          if (identification_result.is_new_speaker) {
            LOG_INFO() << "ProcessOnlineMode: New speaker tracked automatically";
          }
        } else {
          // No speaker identified, clear previous speaker info
          current_speaker_id_.clear();
          current_speaker_name_.clear();
          LOG_DEBUG() << "ProcessOnlineMode: No speaker identified";
        }
      }

      SendSentenceEnd(current_sentence_.index,
                     total_ms_,
                     current_sentence_.begin_time,
                     current_sentence_.result);

      // 重置识别器流，开始新的句子
      online_recognizer_->Reset(online_stream_.get());

      // 清空累积的句子音频
      current_sentence_audio_.clear();

      sentence_counter_++;
      current_sentence_.index = sentence_counter_;
      current_sentence_.begin_time = total_ms_;
      current_sentence_.current_time = total_ms_;
      current_sentence_.result = "";
      current_sentence_.active = true;

      SendSentenceBegin(sentence_counter_, total_ms_);
      LOG_DEBUG() << "ProcessOnlineMode: Started new sentence";
    }
  }

  // 清空缓冲区（在线模式下不需要保留）
  audio_buffer_.clear();
  float_buffer_.clear();

  LOG_DEBUG() << "ProcessOnlineMode finished";
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

std::string ZAsrConnection::AddPunctuation(const std::string& text) {
  if (!punctuation_ || text.empty()) {
    return text;
  }

  try {
    return punctuation_->AddPunctuation(text);
  } catch (const std::exception& e) {
    LOG_ERROR() << "AddPunctuation failed: " << e.what() << ", returning original text";
    return text;
  }
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

  // Add speaker information if available
  if (enable_speaker_identification_ && !current_speaker_id_.empty()) {
    payload["speaker_id"] = current_speaker_id_;
    payload["speaker"] = current_speaker_name_;
  }

  SendProtocolMessage("Result", payload);
}

void ZAsrConnection::SendSentenceEnd(int index, int time_ms, int begin_time,
                                     const std::string& result) {
  json payload;
  payload["idx"] = index;
  payload["time"] = time_ms;
  payload["begin"] = begin_time;
  // 对最终结果添加标点符号
  payload["text"] = AddPunctuation(result);

  // Add speaker information if available
  if (enable_speaker_identification_ && !current_speaker_id_.empty()) {
    payload["speaker_id"] = current_speaker_id_;
    payload["speaker"] = current_speaker_name_;
  }

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
    current_sentence_audio_.clear();
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
  offline_recognizer_.reset();
  online_recognizer_.reset();
  offline_stream_.reset();
  online_stream_.reset();

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
