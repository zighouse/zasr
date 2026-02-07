/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#ifndef ZASR_VOICE_PRINT_DB_H
#define ZASR_VOICE_PRINT_DB_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <cstdint>

#include "yaml-cpp/yaml.h"

namespace zasr {

// 声纹元数据结构
struct VoicePrintMetadata {
  std::string id;                  // 唯一标识符，如 "speaker-001"
  std::string name;                // 用户定义的名称
  std::string created_at;          // ISO 8601 时间戳
  std::string updated_at;          // ISO 8601 时间戳
  std::string embedding_file;      // 相对于数据库根目录的路径
  int32_t embedding_dim = 0;       // embedding 向量维度
  int32_t num_samples = 0;         // 用于提取声纹的音频样本数
  std::vector<std::string> audio_samples;  // 音频样本文件路径列表

  // 扩展元数据
  struct ExtraMetadata {
    std::string gender = "unknown";  // male/female/unknown
    std::string language = "unknown"; // zh-CN/en-US/unknown
    std::string notes;               // 自定义备注
  } metadata;

  // 从 YAML 节点加载
  void FromYaml(const YAML::Node& node);

  // 转换为 YAML 节点
  YAML::Node ToYaml() const;

  // 获取完整的 embedding 文件路径
  std::string GetEmbeddingPath(const std::string& db_base_dir) const;
};

// 未知说话人记录
struct UnknownSpeaker {
  std::string id;                  // unknown-001, unknown-002, ...
  std::string first_seen;          // ISO 8601 时间戳
  std::string embedding_file;      // embedding 文件路径
  int32_t embedding_dim = 0;       // embedding 向量维度
  int32_t occurrence_count = 0;    // 出现次数

  struct ExtraMetadata {
    std::string last_seen;         // 最后出现时间
    float avg_confidence = 0.0f;   // 平均置信度
  } metadata;

  // 从 YAML 节点加载
  void FromYaml(const YAML::Node& node);

  // 转换为 YAML 节点
  YAML::Node ToYaml() const;
};

// 声纹数据库类
class VoicePrintDatabase {
 public:
  // 构造函数
  explicit VoicePrintDatabase(const std::string& db_path = "");

  // 析构函数
  ~VoicePrintDatabase();

  // 禁止拷贝和赋值
  VoicePrintDatabase(const VoicePrintDatabase&) = delete;
  VoicePrintDatabase& operator=(const VoicePrintDatabase&) = delete;

  // 加载数据库（从 YAML 索引文件）
  // Returns true if successful, false on error
  bool Load();

  // 保存数据库（到 YAML 索引文件）
  // Returns true if successful, false on error
  bool Save() const;

  // 添加或更新声纹
  // 如果 speaker_id 已存在，则更新；否则添加新条目
  // Returns true if successful
  bool AddVoicePrint(const VoicePrintMetadata& metadata,
                     const std::vector<float>& embedding);

  // 删除声纹
  // Returns true if speaker was found and removed
  bool RemoveVoicePrint(const std::string& speaker_id);

  // 更新说话人姓名
  // Returns true if speaker was found and updated
  bool UpdateSpeakerName(const std::string& speaker_id, const std::string& new_name);

  // 获取声纹元数据
  // Returns nullptr if speaker_id not found
  const VoicePrintMetadata* GetVoicePrint(const std::string& speaker_id) const;

  // 加载指定说话人的 embedding
  // Returns empty vector if not found or error loading
  std::vector<float> LoadEmbedding(const std::string& speaker_id) const;

  // 获取所有已注册的说话人 ID
  std::vector<std::string> GetAllSpeakerIds() const;

  // 获取所有已注册的说话人元数据
  std::vector<VoicePrintMetadata> GetAllVoicePrints() const;

  // 检查说话人是否存在
  bool Contains(const std::string& speaker_id) const;

  // 获取说话人数量
  size_t GetVoicePrintCount() const { return voice_prints_.size(); }

  // 添加未知说话人记录
  // Returns the assigned unknown speaker ID
  std::string AddUnknownSpeaker(const std::vector<float>& embedding);

  // 更新未知说话人的出现次数
  void UpdateUnknownSpeaker(const std::string& unknown_id,
                           float confidence);

  // 获取所有未知说话人
  std::vector<UnknownSpeaker> GetAllUnknownSpeakers() const;

  // 获取数据库路径
  const std::string& GetDatabasePath() const { return db_path_; }

  // 获取索引文件路径
  std::string GetIndexPath() const;

  // 获取 embeddings 目录路径
  std::string GetEmbeddingsDir() const;

  // 获取 samples 目录路径
  std::string GetSamplesDir() const;

  // 生成新的说话人 ID
  std::string GenerateSpeakerId();

  // 生成新的未知说话人 ID
  std::string GenerateUnknownSpeakerId();

  // 验证数据库完整性
  // Returns true if all referenced files exist
  bool Validate() const;

 private:
  // 创建数据库目录结构
  bool CreateDirectories();

  // 加载 YAML 索引文件
  bool LoadIndex();

  // 保存 YAML 索引文件
  bool SaveIndex() const;

  // 保存 embedding 到文件
  bool SaveEmbedding(const std::string& filepath,
                    const std::vector<float>& embedding) const;

  // 删除 embedding 文件
  bool DeleteEmbedding(const std::string& filepath) const;

 private:
  std::string db_path_;                          // 数据库根目录
  std::string version_ = "1.0";                  // 数据库版本
  std::string created_at_;                       // 数据库创建时间
  std::string updated_at_;                       // 最后更新时间

  std::map<std::string, VoicePrintMetadata> voice_prints_;  // 已知说话人
  std::map<std::string, UnknownSpeaker> unknown_speakers_;  // 未知说话人

  int32_t next_speaker_num_ = 1;      // 用于生成 speaker-001, speaker-002, ...
  int32_t next_unknown_num_ = 1;      // 用于生成 unknown-001, unknown-002, ...
};

}  // namespace zasr

#endif  // ZASR_VOICE_PRINT_DB_H
