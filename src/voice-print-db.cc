/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#include "voice-print-db.h"
#include "zasr-logger.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace zasr {

namespace fs = std::filesystem;

// 辅助函数：获取当前时间戳(ISO 8601格式)
static std::string GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);

  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

// 辅助函数：展开波浪号路径
static std::string ExpandTilde(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    const char* home = std::getenv("HOME");
    if (home) {
      return std::string(home) + path.substr(1);
    }
  }
  return path;
}

// ============================================================================
// VoicePrintMetadata 实现
// ============================================================================

void VoicePrintMetadata::FromYaml(const YAML::Node& node) {
  if (node["id"]) id = node["id"].as<std::string>();
  if (node["name"]) name = node["name"].as<std::string>();
  if (node["created_at"]) created_at = node["created_at"].as<std::string>();
  if (node["updated_at"]) updated_at = node["updated_at"].as<std::string>();
  if (node["embedding_file"]) embedding_file = node["embedding_file"].as<std::string>();
  if (node["embedding_dim"]) embedding_dim = node["embedding_dim"].as<int32_t>();
  if (node["num_samples"]) num_samples = node["num_samples"].as<int32_t>();

  if (node["audio_samples"] && node["audio_samples"].IsSequence()) {
    for (const auto& sample : node["audio_samples"]) {
      audio_samples.push_back(sample.as<std::string>());
    }
  }

  if (node["metadata"]) {
    const auto& meta = node["metadata"];
    if (meta["gender"]) metadata.gender = meta["gender"].as<std::string>();
    if (meta["language"]) metadata.language = meta["language"].as<std::string>();
    if (meta["notes"]) metadata.notes = meta["notes"].as<std::string>();
  }
}

YAML::Node VoicePrintMetadata::ToYaml() const {
  YAML::Node node;
  node["id"] = id;
  node["name"] = name;
  node["created_at"] = created_at;
  node["updated_at"] = updated_at;
  node["embedding_file"] = embedding_file;
  node["embedding_dim"] = embedding_dim;
  node["num_samples"] = num_samples;

  if (!audio_samples.empty()) {
    node["audio_samples"] = audio_samples;
  }

  YAML::Node meta;
  meta["gender"] = metadata.gender;
  meta["language"] = metadata.language;
  meta["notes"] = metadata.notes;
  node["metadata"] = meta;

  return node;
}

std::string VoicePrintMetadata::GetEmbeddingPath(
    const std::string& db_base_dir) const {
  return db_base_dir + "/" + embedding_file;
}

// ============================================================================
// UnknownSpeaker 实现
// ============================================================================

void UnknownSpeaker::FromYaml(const YAML::Node& node) {
  if (node["id"]) id = node["id"].as<std::string>();
  if (node["first_seen"]) first_seen = node["first_seen"].as<std::string>();
  if (node["embedding_file"]) embedding_file = node["embedding_file"].as<std::string>();
  if (node["embedding_dim"]) embedding_dim = node["embedding_dim"].as<int32_t>();
  if (node["occurrence_count"]) occurrence_count = node["occurrence_count"].as<int32_t>();

  if (node["metadata"]) {
    const auto& meta = node["metadata"];
    if (meta["last_seen"]) metadata.last_seen = meta["last_seen"].as<std::string>();
    if (meta["avg_confidence"]) metadata.avg_confidence = meta["avg_confidence"].as<float>();
  }
}

YAML::Node UnknownSpeaker::ToYaml() const {
  YAML::Node node;
  node["id"] = id;
  node["first_seen"] = first_seen;
  node["embedding_file"] = embedding_file;
  node["embedding_dim"] = embedding_dim;
  node["occurrence_count"] = occurrence_count;

  YAML::Node meta;
  meta["last_seen"] = metadata.last_seen;
  meta["avg_confidence"] = metadata.avg_confidence;
  node["metadata"] = meta;

  return node;
}

// ============================================================================
// VoicePrintDatabase 实现
// ============================================================================

VoicePrintDatabase::VoicePrintDatabase(const std::string& db_path)
    : db_path_(ExpandTilde(db_path)) {
   // If没有指定路径，使用默认路径
  if (db_path_.empty()) {
    const char* home = std::getenv("HOME");
    if (home) {
      db_path_ = std::string(home) + "/.zasr/voice-prints";
    } else {
      db_path_ = "/tmp/zasr/voice-prints";
    }
  }

  created_at_ = GetCurrentTimestamp();
  updated_at_ = created_at_;
}

VoicePrintDatabase::~VoicePrintDatabase() {
  // 自动保存
  if (!voice_prints_.empty() || !unknown_speakers_.empty()) {
    Save();
  }
}

bool VoicePrintDatabase::CreateDirectories() {
  try {
    fs::create_directories(GetEmbeddingsDir());
    fs::create_directories(GetSamplesDir());
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Error creating directories: " << e.what() << std::endl;
    return false;
  }
}

std::string VoicePrintDatabase::GetIndexPath() const {
  return db_path_ + "/voice-prints.yaml";
}

std::string VoicePrintDatabase::GetEmbeddingsDir() const {
  return db_path_ + "/embeddings";
}

std::string VoicePrintDatabase::GetSamplesDir() const {
  return db_path_ + "/samples";
}

bool VoicePrintDatabase::Load() {
  // 确保目录存在
  if (!fs::exists(db_path_)) {
    CreateDirectories();
    // 新数据库，无需加载
    return true;
  }

  return LoadIndex();
}

bool VoicePrintDatabase::LoadIndex() {
  std::string index_path = GetIndexPath();

  if (!fs::exists(index_path)) {
    // 索引文件不存在，这是新数据库
    LOG_INFO() << "Voice print DB index file does not exist, creating new DB: " << index_path;
    return CreateDirectories();
  }

  try {
    YAML::Node root = YAML::LoadFile(index_path);

    // 加载版本和元数据
    if (root["version"]) version_ = root["version"].as<std::string>();
    if (root["created_at"]) created_at_ = root["created_at"].as<std::string>();
    if (root["updated_at"]) updated_at_ = root["updated_at"].as<std::string>();

    // 加载已知说话人
    if (root["voice_prints"] && root["voice_prints"].IsSequence()) {
      for (const auto& vp_node : root["voice_prints"]) {
        VoicePrintMetadata metadata;
        metadata.FromYaml(vp_node);
        voice_prints_[metadata.id] = metadata;

        // 更新 next_speaker_num_
        if (metadata.id.find("speaker-") == 0) {
          std::string num_str = metadata.id.substr(8); // 跳过 "speaker-"
          try {
            int num = std::stoi(num_str);
            if (num >= next_speaker_num_) {
              next_speaker_num_ = num + 1;
            }
          } catch (...) {
            // 忽略转换错误
          }
        }
      }
    }

    // 加载未知说话人
    if (root["unknown_speakers"] && root["unknown_speakers"].IsSequence()) {
      for (const auto& us_node : root["unknown_speakers"]) {
        UnknownSpeaker unknown;
        unknown.FromYaml(us_node);
        unknown_speakers_[unknown.id] = unknown;

        // 更新 next_unknown_num_
        if (unknown.id.find("unknown-") == 0) {
          std::string num_str = unknown.id.substr(8); // 跳过 "unknown-"
          try {
            int num = std::stoi(num_str);
            if (num >= next_unknown_num_) {
              next_unknown_num_ = num + 1;
            }
          } catch (...) {
            // 忽略转换错误
          }
        }
      }
    }

    LOG_INFO() << "Successfully loaded voice print DB: " << index_path;
    LOG_INFO() << "  Known speakers: " << std::to_string(voice_prints_.size());
    LOG_INFO() << "  Unknown speakers: " << std::to_string(unknown_speakers_.size());

    return true;

  } catch (const std::exception& e) {
    std::cerr << "Error loading voice print database: " << e.what() << std::endl;
    return false;
  }
}

bool VoicePrintDatabase::Save() const {
  return SaveIndex();
}

bool VoicePrintDatabase::SaveIndex() const {
  try {
    YAML::Node root;

    root["version"] = version_;
    root["created_at"] = created_at_;
    root["updated_at"] = GetCurrentTimestamp();

    // 保存已知说话人
    YAML::Node vps_node;
    for (const auto& [id, metadata] : voice_prints_) {
      vps_node.push_back(metadata.ToYaml());
    }
    root["voice_prints"] = vps_node;

    // 保存未知说话人
    if (!unknown_speakers_.empty()) {
      YAML::Node us_node;
      for (const auto& [id, unknown] : unknown_speakers_) {
        us_node.push_back(unknown.ToYaml());
      }
      root["unknown_speakers"] = us_node;
    }

    // 确保目录存在
    fs::create_directories(db_path_);

    // 写入文件
    std::string index_path = GetIndexPath();
    std::ofstream out(index_path);
    if (!out) {
      std::cerr << "Error opening index file for writing: " << index_path << std::endl;
      return false;
    }

    out << root;
    out.close();

    LOG_INFO() << "Successfully saved voice print DB: " << index_path;
    return true;

  } catch (const std::exception& e) {
    std::cerr << "Error saving voice print database: " << e.what() << std::endl;
    return false;
  }
}

bool VoicePrintDatabase::SaveEmbedding(
    const std::string& filepath,
    const std::vector<float>& embedding) const {
  try {
    // 确保目录存在
    fs::create_directories(fs::path(filepath).parent_path());

    std::ofstream out(filepath, std::ios::binary);
    if (!out) {
      std::cerr << "Error opening embedding file for writing: " << filepath << std::endl;
      return false;
    }

    int32_t dim = static_cast<int32_t>(embedding.size());
    out.write(reinterpret_cast<const char*>(&dim), sizeof(dim));
    out.write(reinterpret_cast<const char*>(embedding.data()),
             embedding.size() * sizeof(float));

    out.close();
    return true;

  } catch (const std::exception& e) {
    std::cerr << "Error saving embedding: " << e.what() << std::endl;
    return false;
  }
}

bool VoicePrintDatabase::DeleteEmbedding(const std::string& filepath) const {
  try {
    if (fs::exists(filepath)) {
      fs::remove(filepath);
      LOG_INFO() << "Delete embedding file: " << filepath;
    }
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Error deleting embedding file: " << e.what() << std::endl;
    return false;
  }
}

bool VoicePrintDatabase::AddVoicePrint(
    const VoicePrintMetadata& metadata,
    const std::vector<float>& embedding) {

  // 确保目录存在
  CreateDirectories();

  // 保存 embedding 到文件
  std::string embedding_path = metadata.GetEmbeddingPath(db_path_);
  if (!SaveEmbedding(embedding_path, embedding)) {
    std::cerr << "Failed to save embedding for speaker: " << metadata.id << std::endl;
    return false;
  }

   // Add or update metadata
  voice_prints_[metadata.id] = metadata;
  updated_at_ = GetCurrentTimestamp();

  LOG_INFO() << "Add/update voice print: " << metadata.id << " (" << metadata.name << ")";

  return true;
}

bool VoicePrintDatabase::RemoveVoicePrint(const std::string& speaker_id) {
  auto it = voice_prints_.find(speaker_id);
  if (it == voice_prints_.end()) {
    return false;
  }

   // Delete embedding file
  std::string embedding_path = it->second.GetEmbeddingPath(db_path_);
  DeleteEmbedding(embedding_path);

   // Remove from database
  voice_prints_.erase(it);
  updated_at_ = GetCurrentTimestamp();

  LOG_INFO() << "Remove voice print: " << speaker_id;
  return true;
}

bool VoicePrintDatabase::UpdateSpeakerName(const std::string& speaker_id,
                                           const std::string& new_name) {
  auto it = voice_prints_.find(speaker_id);
  if (it == voice_prints_.end()) {
    return false;
  }

  it->second.name = new_name;
  it->second.updated_at = GetCurrentTimestamp();
  updated_at_ = GetCurrentTimestamp();

  LOG_INFO() << "Update speaker name: " << speaker_id << " -> " << new_name;
  return true;
}

const VoicePrintMetadata* VoicePrintDatabase::GetVoicePrint(
    const std::string& speaker_id) const {
  auto it = voice_prints_.find(speaker_id);
  if (it != voice_prints_.end()) {
    return &(it->second);
  }
  return nullptr;
}

std::vector<float> VoicePrintDatabase::LoadEmbedding(
    const std::string& speaker_id) const {
  auto it = voice_prints_.find(speaker_id);
  if (it == voice_prints_.end()) {
    return {};
  }

  std::string embedding_path = it->second.GetEmbeddingPath(db_path_);

  try {
    std::ifstream in(embedding_path, std::ios::binary);
    if (!in) {
      std::cerr << "Error opening embedding file: " << embedding_path << std::endl;
      return {};
    }

    int32_t dim;
    in.read(reinterpret_cast<char*>(&dim), sizeof(dim));

    std::vector<float> embedding(dim);
    in.read(reinterpret_cast<char*>(embedding.data()), dim * sizeof(float));

    if (!in) {
      std::cerr << "Error reading embedding file: " << embedding_path << std::endl;
      return {};
    }

    in.close();
    return embedding;

  } catch (const std::exception& e) {
    std::cerr << "Error loading embedding: " << e.what() << std::endl;
    return {};
  }
}

std::vector<std::string> VoicePrintDatabase::GetAllSpeakerIds() const {
  std::vector<std::string> ids;
  ids.reserve(voice_prints_.size());
  for (const auto& [id, _] : voice_prints_) {
    ids.push_back(id);
  }
  return ids;
}

std::vector<VoicePrintMetadata> VoicePrintDatabase::GetAllVoicePrints() const {
  std::vector<VoicePrintMetadata> metadata_list;
  metadata_list.reserve(voice_prints_.size());
  for (const auto& [id, metadata] : voice_prints_) {
    metadata_list.push_back(metadata);
  }
  return metadata_list;
}

bool VoicePrintDatabase::Contains(const std::string& speaker_id) const {
  return voice_prints_.find(speaker_id) != voice_prints_.end();
}

std::string VoicePrintDatabase::GenerateSpeakerId() {
  std::string id;
  do {
    id = "speaker-" + std::to_string(next_speaker_num_++);
  } while (Contains(id));
  return id;
}

std::string VoicePrintDatabase::AddUnknownSpeaker(
    const std::vector<float>& embedding) {
  std::string id = GenerateUnknownSpeakerId();

  UnknownSpeaker unknown;
  unknown.id = id;
  unknown.first_seen = GetCurrentTimestamp();
  unknown.embedding_dim = static_cast<int32_t>(embedding.size());
  unknown.occurrence_count = 1;
  unknown.metadata.last_seen = unknown.first_seen;
  unknown.metadata.avg_confidence = 0.0f;

  // 保存 embedding
  std::string embeddings_dir = GetEmbeddingsDir();
  unknown.embedding_file = "embeddings/" + id + ".bin";
  std::string embedding_path = db_path_ + "/" + unknown.embedding_file;

  if (!SaveEmbedding(embedding_path, embedding)) {
    std::cerr << "Failed to save embedding for unknown speaker: " << id << std::endl;
    return "";
  }

  unknown_speakers_[id] = unknown;
  updated_at_ = GetCurrentTimestamp();

  LOG_INFO() << "Add unknown speaker: " << id;
  return id;
}

void VoicePrintDatabase::UpdateUnknownSpeaker(
    const std::string& unknown_id, float confidence) {
  auto it = unknown_speakers_.find(unknown_id);
  if (it == unknown_speakers_.end()) {
    return;
  }

  UnknownSpeaker& unknown = it->second;
  unknown.occurrence_count++;
  unknown.metadata.last_seen = GetCurrentTimestamp();

  // 更新平均置信度(简单移动平均)
  float old_avg = unknown.metadata.avg_confidence;
  int count = unknown.occurrence_count;
  unknown.metadata.avg_confidence =
      (old_avg * (count - 1) + confidence) / count;

  updated_at_ = GetCurrentTimestamp();
}

std::vector<UnknownSpeaker> VoicePrintDatabase::GetAllUnknownSpeakers() const {
  std::vector<UnknownSpeaker> unknown_list;
  unknown_list.reserve(unknown_speakers_.size());
  for (const auto& [id, unknown] : unknown_speakers_) {
    unknown_list.push_back(unknown);
  }
  return unknown_list;
}

std::string VoicePrintDatabase::GenerateUnknownSpeakerId() {
  std::string id;
  do {
    id = "unknown-" + std::to_string(next_unknown_num_++);
  } while (unknown_speakers_.find(id) != unknown_speakers_.end());
  return id;
}

bool VoicePrintDatabase::Validate() const {
  bool valid = true;

  for (const auto& [id, metadata] : voice_prints_) {
    std::string embedding_path = metadata.GetEmbeddingPath(db_path_);
    if (!fs::exists(embedding_path)) {
      std::cerr << "Missing embedding file: " << embedding_path << std::endl;
      valid = false;
    }
  }

  for (const auto& [id, unknown] : unknown_speakers_) {
    std::string embedding_path = db_path_ + "/" + unknown.embedding_file;
    if (!fs::exists(embedding_path)) {
      std::cerr << "Missing embedding file: " << embedding_path << std::endl;
      valid = false;
    }
  }

  return valid;
}

}  // namespace zasr
