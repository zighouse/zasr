/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#include "speaker-identifier.h"
#include "zasr-logger.h"

#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <chrono>
#include <iomanip>

namespace zasr {

// Forward declaration from voice-print-manager.h
std::string GetCurrentTimestamp();

// ============================================================================
// ZSpeakerIdentifier 实现
// ============================================================================

ZSpeakerIdentifier::ZSpeakerIdentifier(const Config& config)
    : config_(config) {
}

ZSpeakerIdentifier::~ZSpeakerIdentifier() {
   // C API objects auto freed via unique_ptr
}

bool ZSpeakerIdentifier::Initialize() {
  if (initialized_) {
    LOG_WARN() << "ZSpeakerIdentifier already initialized";
    return true;
  }

  // 1. 初始化 SpeakerEmbeddingExtractor
  SherpaOnnxSpeakerEmbeddingExtractorConfig c_config;
  std::memset(&c_config, 0, sizeof(c_config));
  c_config.model = config_.model.c_str();
  c_config.num_threads = config_.num_threads;
  c_config.debug = config_.debug ? 1 : 0;
  c_config.provider = config_.provider.c_str();

  extractor_.reset(const_cast<SherpaOnnxSpeakerEmbeddingExtractor*>(
      SherpaOnnxCreateSpeakerEmbeddingExtractor(&c_config)));
  if (!extractor_) {
    LOG_ERROR() << "Failed to create SpeakerEmbeddingExtractor";
    return false;
  }

  int32_t dim = SherpaOnnxSpeakerEmbeddingExtractorDim(extractor_.get());
  LOG_INFO() << "SpeakerEmbeddingExtractor created successfully, embedding dim: " << std::to_string(dim);

  // 2. 初始化 SpeakerEmbeddingManager
  manager_.reset(const_cast<SherpaOnnxSpeakerEmbeddingManager*>(
      SherpaOnnxCreateSpeakerEmbeddingManager(dim)));
  if (!manager_) {
    LOG_ERROR() << "Failed to create SpeakerEmbeddingManager";
    return false;
  }

  LOG_INFO() << "SpeakerEmbeddingManager created successfully";

  // 3. 加载声纹数据库
  database_ = std::make_unique<VoicePrintDatabase>(config_.voice_print_db);
  if (!database_->Load()) {
    LOG_WARN() << "Cannot load database, will create new database";
  }

  // 4. 从数据库加载已注册的说话人到 manager
  auto speaker_ids = database_->GetAllSpeakerIds();
  for (const auto& speaker_id : speaker_ids) {
    auto metadata = database_->GetVoicePrint(speaker_id);
    if (metadata) {
      auto embedding = database_->LoadEmbedding(speaker_id);
      if (!embedding.empty()) {
        // 添加到 C API manager
        // 注意：C API 的 AddList 需要 embedding 数组，我们需要构造它
        std::vector<const float*> embedding_ptrs;
        embedding_ptrs.push_back(embedding.data());

        if (!SherpaOnnxSpeakerEmbeddingManagerAddList(
                manager_.get(), metadata->name.c_str(),
                embedding_ptrs.data())) {
          LOG_WARN() << "Failed to load voice print to manager: " << speaker_id;
        } else {
          LOG_INFO() << "Loaded voice print: " << speaker_id << " (" << metadata->name << ")";
        }
      }
    }
  }

  LOG_INFO() << "Total loaded "
              << std::to_string(SherpaOnnxSpeakerEmbeddingManagerNumSpeakers(manager_.get()))
              << " speaker voice prints to manager";

  initialized_ = true;
  return true;
}

std::vector<float> ZSpeakerIdentifier::ExtractEmbedding(
    const std::vector<float>& samples) {
  if (!initialized_) {
    LOG_ERROR() << "ZSpeakerIdentifier not initialized";
    return {};
  }

  try {
    SherpaStreamWrapper stream(extractor_.get());

    // 送入音频数据
    SherpaOnnxOnlineStreamAcceptWaveform(stream.Get(), 16000,  // 假设 16kHz
                                       samples.data(), samples.size());
    SherpaOnnxOnlineStreamInputFinished(stream.Get());

    // 检查是否准备好
    if (!SherpaOnnxSpeakerEmbeddingExtractorIsReady(extractor_.get(), stream.Get())) {
      LOG_WARN() << "Audio segment too short to extract embedding";
      return {};
    }

    // 计算 embedding
    const float* embedding_ptr =
        SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(extractor_.get(), stream.Get());

    if (!embedding_ptr) {
      LOG_ERROR() << "Failed to extract embedding";
      return {};
    }

    // 复制 embedding 数据
    int32_t dim = SherpaOnnxSpeakerEmbeddingExtractorDim(extractor_.get());
    std::vector<float> embedding(embedding_ptr, embedding_ptr + dim);

    LOG_INFO() << "Successfully extracted embedding, dim: " << std::to_string(dim);

    return embedding;

  } catch (const std::exception& e) {
    LOG_ERROR() << "Error extracting embedding: " << std::string(e.what());
    return {};
  }
}

std::vector<float> ZSpeakerIdentifier::ExtractEmbeddingFromWav(
    const std::string& wav_path) {
  if (!initialized_) {
    LOG_ERROR() << "ZSpeakerIdentifier not initialized";
    return {};
  }

  try {
    // 读取 WAV 文件
    SherpaWaveWrapper wave(wav_path);

    // 提取 embedding
    SherpaStreamWrapper stream(extractor_.get());

    SherpaOnnxOnlineStreamAcceptWaveform(stream.Get(),
                                       wave.GetSampleRate(),
                                       wave.GetSamples(),
                                       wave.GetNumSamples());
    SherpaOnnxOnlineStreamInputFinished(stream.Get());

    if (!SherpaOnnxSpeakerEmbeddingExtractorIsReady(extractor_.get(), stream.Get())) {
      LOG_WARN() << "Audio file too short: " << wav_path;
      return {};
    }

    const float* embedding_ptr =
        SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(extractor_.get(), stream.Get());

    if (!embedding_ptr) {
      return {};
    }

    int32_t dim = SherpaOnnxSpeakerEmbeddingExtractorDim(extractor_.get());
    std::vector<float> embedding(embedding_ptr, embedding_ptr + dim);

    LOG_INFO() << "From " << wav_path << " extracted embedding successfully";

    return embedding;

  } catch (const std::exception& e) {
    LOG_ERROR() << "From " << wav_path << " Failed to extract embedding: " << e.what();
    return {};
  }
}

ZSpeakerIdentifier::IdentificationResult ZSpeakerIdentifier::MatchSpeaker(
    const std::vector<float>& embedding) {
  IdentificationResult result;

  if (embedding.empty()) {
    return result;
  }

  // 使用 C API 的 Search 函数
  const char* name_ptr = SherpaOnnxSpeakerEmbeddingManagerSearch(
      manager_.get(), embedding.data(), config_.similarity_threshold);

  if (name_ptr) {
    // 找到匹配的说话人
    result.speaker_name = name_ptr;
    result.confidence = config_.similarity_threshold;  // TODO: 获取实际置信度

    // 释放字符串
    SherpaOnnxSpeakerEmbeddingManagerFreeSearch(name_ptr);

    // 查找 speaker_id
    auto all_speakers = database_->GetAllVoicePrints();
    for (const auto& speaker : all_speakers) {
      if (speaker.name == result.speaker_name) {
        result.speaker_id = speaker.id;
        break;
      }
    }

    LOG_INFO() << "Identified speaker: " << result.speaker_id << " (" << result.speaker_name << ")";

  } else {
    // No matching speaker found
    LOG_INFO() << "No matching speaker found (threshold: " << std::to_string(config_.similarity_threshold) << ")";
  }

  return result;
}

std::string ZSpeakerIdentifier::CreateUnknownSpeaker(
    const std::vector<float>& embedding) {
  if (!config_.enable_auto_track) {
    return "";
  }

  // 使用数据库创建unknown说话人
  std::string unknown_id = database_->AddUnknownSpeaker(embedding);
  if (unknown_id.empty()) {
    LOG_ERROR() << "Failed to create unknown speaker record";
    return "";
  }

    LOG_INFO() << "Created new speaker record: " << unknown_id;

  return unknown_id;
}

bool ZSpeakerIdentifier::VerifySpeaker(
    const std::string& name,
    const std::vector<float>& embedding) {
  if (!initialized_) {
    return false;
  }

  int32_t ok = SherpaOnnxSpeakerEmbeddingManagerVerify(
      manager_.get(), name.c_str(), embedding.data(), config_.similarity_threshold);

  LOG_INFO() << "Verify speaker " << name << ": " << (ok ? "SUCCESS" : "FAILED");

  return ok != 0;
}

ZSpeakerIdentifier::IdentificationResult ZSpeakerIdentifier::ProcessSegment(
    const std::vector<float>& samples) {
  IdentificationResult result;

  if (!initialized_) {
    LOG_ERROR() << "ZSpeakerIdentifier not initialized";
    return result;
  }

  // 提取 embedding
  auto embedding = ExtractEmbedding(samples);
  if (embedding.empty()) {
    return result;
  }

   // Match speaker
  result = MatchSpeaker(embedding);

   // If no matching speaker found and auto-track enabled
  if (result.speaker_id.empty() && config_.enable_auto_track) {
    std::string unknown_id = CreateUnknownSpeaker(embedding);
    if (!unknown_id.empty()) {
      result.speaker_id = unknown_id;
      result.speaker_name = "Unknown Speaker";   // TODO: generate friendly name
      result.is_new_speaker = true;
    }
  }

  return result;
}

ZSpeakerIdentifier::IdentificationResult ZSpeakerIdentifier::IdentifyFromWav(
    const std::string& wav_path) {
  IdentificationResult result;

  if (!initialized_) {
    LOG_ERROR() << "ZSpeakerIdentifier not initialized";
    return result;
  }

  // From WAV 文件提取 embedding
  auto embedding = ExtractEmbeddingFromWav(wav_path);
  if (embedding.empty()) {
    return result;
  }

   // Match speaker
  result = MatchSpeaker(embedding);

   // If no matching speaker found and auto-track enabled
  if (result.speaker_id.empty() && config_.enable_auto_track) {
    std::string unknown_id = CreateUnknownSpeaker(embedding);
    if (!unknown_id.empty()) {
      result.speaker_id = unknown_id;
      result.speaker_name = "Unknown Speaker";
      result.is_new_speaker = true;
    }
  }

  return result;
}

bool ZSpeakerIdentifier::AddSpeaker(
    const std::string& name,
    const std::vector<std::string>& wav_files) {
  if (!initialized_) {
    LOG_ERROR() << "ZSpeakerIdentifier not initialized";
    return false;
  }

  if (wav_files.empty()) {
    LOG_ERROR() << "Audio file list is empty";
    return false;
  }

  // 提取所有 embedding
  std::vector<std::vector<float>> embeddings;
  embeddings.reserve(wav_files.size());

  for (const auto& wav_file : wav_files) {
    auto embedding = ExtractEmbeddingFromWav(wav_file);
    if (embedding.empty()) {
      LOG_WARN() << "Skip failed audio file: " << wav_file;
      continue;
    }
    embeddings.push_back(embedding);
  }

  if (embeddings.empty()) {
    LOG_ERROR() << "Failed to extract embedding from any audio file";
    return false;
  }

  // 构造 C API 需要的 embedding 指针数组
  std::vector<const float*> embedding_ptrs;
  embedding_ptrs.reserve(embeddings.size());
  for (const auto& emb : embeddings) {
    embedding_ptrs.push_back(emb.data());
  }

  // 使用 C API 添加到 manager
  if (!SherpaOnnxSpeakerEmbeddingManagerAddList(
          manager_.get(), name.c_str(), embedding_ptrs.data())) {
    LOG_ERROR() << "Failed to add speaker to manager: " << name;
    return false;
  }

   // Save to database
  std::string speaker_id = database_->GenerateSpeakerId();

   // Calculate average embedding (simplified: use first embedding)
   // TODO: implement true average
  std::vector<float> avg_embedding = embeddings[0];

  VoicePrintMetadata metadata;
  metadata.id = speaker_id;
  metadata.name = name;
  metadata.created_at = GetCurrentTimestamp();
  metadata.updated_at = metadata.created_at;
  metadata.embedding_dim = static_cast<int32_t>(avg_embedding.size());
  metadata.num_samples = static_cast<int32_t>(wav_files.size());
  metadata.embedding_file = "embeddings/" + speaker_id + ".bin";
  metadata.audio_samples = wav_files;  // 保存文件路径

  if (!database_->AddVoicePrint(metadata, avg_embedding)) {
    LOG_ERROR() << "Failed to save to database";
    // Remove from manager
    SherpaOnnxSpeakerEmbeddingManagerRemove(manager_.get(), name.c_str());
    return false;
  }

  LOG_INFO() << "Successfully added speaker: " << speaker_id << " (" << name << ")";

  return true;
}

}  // namespace zasr
