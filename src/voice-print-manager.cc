/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#include "voice-print-manager.h"
#include "speaker-identifier.h"
#include "zasr-logger.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <filesystem>

namespace zasr {

// ============================================================================
// VoicePrintManager::Impl 实现
// ============================================================================

class VoicePrintManager::Impl {
 public:
  Impl(const VoicePrintCollectionConfig& config)
      : config_(config),
        db_(config.db_path.empty() ?
            ExpandPath("~/.zasr/voice-prints") : config.db_path) {
  }

  bool Initialize() {
    // 初始化数据库
    if (!db_.Load()) {
      LOG_WARN() << "Cannot load database, will create new database";
    }

    // 初始化说话人识别器
    ZSpeakerIdentifier::Config sid_config;
    sid_config.model = config_.model;
    sid_config.num_threads = config_.num_threads;
    sid_config.debug = config_.debug;
    sid_config.provider = config_.provider;
    sid_config.voice_print_db = db_.GetDatabasePath();
    sid_config.similarity_threshold = 0.75f;
    sid_config.enable_auto_track = true;

    identifier_ = std::make_unique<ZSpeakerIdentifier>(sid_config);

    if (!identifier_->Initialize()) {
      LOG_ERROR() << "Failed to initialize SpeakerIdentifier";
      return false;
    }

    LOG_INFO() << "VoicePrintManager initialized successfully";
    return true;
  }

  VoicePrintDatabase& GetDatabase() { return db_; }
  ZSpeakerIdentifier& GetIdentifier() { return *identifier_; }

 private:
  VoicePrintCollectionConfig config_;
  VoicePrintDatabase db_;
  std::unique_ptr<ZSpeakerIdentifier> identifier_;
};

// ============================================================================
// VoicePrintCollectionConfig 实现
// ============================================================================

bool VoicePrintCollectionConfig::Validate() const {
  if (model.empty()) {
    std::cerr << "Error: speaker embedding model path not specified" << std::endl;
    return false;
  }

  // Check if model path exists
  if (!std::filesystem::exists(model)) {
    std::cerr << "Error: model path does not exist: " << model << std::endl;
    return false;
  }

  if (num_threads <= 0) {
    std::cerr << "Error: num_threads must be greater than 0" << std::endl;
    return false;
  }

  if (sample_rate != 16000) {
    std::cerr << "Warning: only 16kHz sample rate is currently supported" << std::endl;
  }

  if (min_duration < 1.0f) {
    std::cerr << "Warning: min_duration too short, recommend at least 3 seconds" << std::endl;
  }

  if (max_duration > 60.0f) {
    std::cerr << "Warning: max_duration too long, recommend not exceeding 30 seconds" << std::endl;
  }

  return true;
}

std::string VoicePrintCollectionConfig::ToString() const {
  std::ostringstream oss;
  oss << "VoicePrintCollectionConfig {\n"
      << "  model: " << model << "\n"
      << "  num_threads: " << num_threads << "\n"
      << "  debug: " << (debug ? "true" : "false") << "\n"
      << "  provider: " << provider << "\n"
      << "  db_path: " << db_path << "\n"
      << "  sample_rate: " << sample_rate << "\n"
      << "  min_duration: " << min_duration << "\n"
      << "  max_duration: " << max_duration << "\n"
      << "}";
  return oss.str();
}

// ============================================================================
// VoicePrintManager 实现
// ============================================================================

VoicePrintManager::VoicePrintManager(const VoicePrintCollectionConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

VoicePrintManager::~VoicePrintManager() = default;

bool VoicePrintManager::Initialize() {
  return impl_->Initialize();
}

std::vector<float> VoicePrintManager::ExtractEmbedding(
    const std::string& audio_file) {
  auto result = impl_->GetIdentifier().IdentifyFromWav(audio_file);

  if (result.speaker_id.empty()) {
    LOG_ERROR() << "Failed to extract embedding from audio file: " << audio_file;
    return {};
  }

  // 读取 embedding 从数据库(如果是已知的说话人)
  // 或者从unknown说话人返回 embedding
  // 这里简化处理，实际上应该从识别过程中获取 embedding
   // TODO: implement logic to return actual embedding
  return {};
}

std::vector<float> VoicePrintManager::ExtractEmbedding(
    const std::vector<std::string>& audio_files) {
  // 提取多 embedding 并计算平均
   // TODO: implement actual embedding extraction and averaging
  return {};
}

std::string VoicePrintManager::AddSpeaker(
    const std::string& name,
    const std::vector<std::string>& audio_files,
    const std::string& gender,
    const std::string& language,
    const std::string& notes) {
   // Add speaker using ZSpeakerIdentifier (now returns speaker_id directly)
  std::string speaker_id = impl_->GetIdentifier().AddSpeaker(name, audio_files);
  if (speaker_id.empty()) {
    LOG_ERROR() << "Failed to add speaker: " << name;
    return "";
  }

  LOG_INFO() << "Successfully added speaker: " << speaker_id << " (" << name << ")";
  return speaker_id;
}

std::string VoicePrintManager::AddSpeaker(
    const std::string& name,
    const std::vector<float>& embedding,
    const std::string& gender,
    const std::string& language,
    const std::string& notes) {
  VoicePrintMetadata metadata;
  metadata.id = impl_->GetDatabase().GenerateSpeakerId();
  metadata.name = name;
  metadata.created_at = GetCurrentTimestamp();
  metadata.updated_at = metadata.created_at;
  metadata.embedding_dim = static_cast<int32_t>(embedding.size());
  metadata.num_samples = 1;
  metadata.embedding_file = "embeddings/" + metadata.id + ".bin";
  metadata.metadata.gender = gender;
  metadata.metadata.language = language;
  metadata.metadata.notes = notes;

  if (!impl_->GetDatabase().AddVoicePrint(metadata, embedding)) {
    LOG_ERROR() << "Failed to save to database";
    return "";
  }

  LOG_INFO() << "Successfully added speaker: " << metadata.id << " (" << name << ")";
  return metadata.id;
}

bool VoicePrintManager::RemoveSpeaker(const std::string& speaker_id) {
  if (!impl_->GetDatabase().RemoveVoicePrint(speaker_id)) {
    LOG_ERROR() << "Failed to remove speaker: " << speaker_id;
    return false;
  }

  LOG_INFO() << "Successfully removed speaker: " << speaker_id;
  return true;
}

bool VoicePrintManager::RenameSpeaker(
    const std::string& speaker_id,
    const std::string& new_name) {
  if (!impl_->GetDatabase().UpdateSpeakerName(speaker_id, new_name)) {
    LOG_ERROR() << "Failed to rename speaker: " << speaker_id;
    return false;
  }

  LOG_INFO() << "Successfully renamed speaker: " << speaker_id << " -> " << new_name;
  return true;
}

std::string VoicePrintManager::IdentifySpeaker(
    const std::string& audio_file,
    float* out_confidence) {
  auto result = impl_->GetIdentifier().IdentifyFromWav(audio_file);

  if (out_confidence) {
    *out_confidence = result.confidence;
  }

  return result.speaker_id;
}

bool VoicePrintManager::VerifySpeaker(
    const std::string& speaker_id,
    const std::string& audio_file,
    float threshold) {
  auto metadata = impl_->GetDatabase().GetVoicePrint(speaker_id);
  if (!metadata) {
    LOG_ERROR() << "Speaker not found: " << speaker_id;
    return false;
  }

  auto result = impl_->GetIdentifier().IdentifyFromWav(audio_file);

   // If识别到的 speaker_id 匹配，且置信度足够高
  return (result.speaker_id == speaker_id && result.confidence >= threshold);
}

std::vector<VoicePrintMetadata> VoicePrintManager::ListSpeakers() const {
  return const_cast<VoicePrintManager*>(this)->impl_->GetDatabase().GetAllVoicePrints();
}

std::unique_ptr<VoicePrintMetadata> VoicePrintManager::GetSpeakerInfo(
    const std::string& speaker_id) const {
  const VoicePrintMetadata* metadata =
      const_cast<VoicePrintManager*>(this)->impl_->GetDatabase().GetVoicePrint(speaker_id);
  if (metadata) {
    return std::make_unique<VoicePrintMetadata>(*metadata);
  }
  return nullptr;
}

size_t VoicePrintManager::GetSpeakerCount() const {
  return const_cast<VoicePrintManager*>(this)->impl_->GetDatabase().GetVoicePrintCount();
}

// 辅助函数
std::string GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  std::ostringstream oss;
  oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::string ExpandPath(const std::string& path) {
  if (path.empty()) {
    return path;
  }

  if (path[0] == '~') {
    const char* home = getenv("HOME");
    if (home) {
      return std::string(home) + path.substr(1);
    }
  }

  return path;
}

}  // namespace zasr
