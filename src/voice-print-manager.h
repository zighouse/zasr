/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#ifndef ZASR_VOICE_PRINT_MANAGER_H
#define ZASR_VOICE_PRINT_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <cstdint>
#include <iostream>

#include "voice-print-db.h"

namespace zasr {

// 前置声明
class ZSpeakerIdentifier;

// 声纹采集配置
struct VoicePrintCollectionConfig {
  std::string model;              // sherpa-onnx speaker embedding 模型路径
  int32_t num_threads = 2;        // 线程数
  bool debug = false;
  std::string provider = "cpu";
  std::string db_path;            // 数据库路径（可选，默认为 ~/.zasr/voice-prints）

  // 音频配置
  int32_t sample_rate = 16000;
  float min_duration = 3.0f;      // 最小音频时长（秒）
  float max_duration = 30.0f;     // 最大音频时长（秒）

  bool Validate() const;
  std::string ToString() const;
};

// 声纹管理器类
// 负责：
// 1. 从音频提取声纹特征
// 2. 管理声纹数据库
// 3. 识别说话人
class VoicePrintManager {
 public:
  // 构造函数
  explicit VoicePrintManager(const VoicePrintCollectionConfig& config);

  // 析构函数
  ~VoicePrintManager();

  // 禁止拷贝和赋值
  VoicePrintManager(const VoicePrintManager&) = delete;
  VoicePrintManager& operator=(const VoicePrintManager&) = delete;

  // 初始化（加载模型和数据库）
  // Returns true if successful
  bool Initialize();

  // 从音频文件提取声纹特征
  // Returns empty vector if failed
  std::vector<float> ExtractEmbedding(const std::string& audio_file);

  // 从多个音频文件提取声纹特征，并计算平均
  // Returns empty vector if failed
  std::vector<float> ExtractEmbedding(
      const std::vector<std::string>& audio_files);

  // 添加说话人声纹
  // force: 如果为 true，跳过多说话人检测
  // Returns speaker_id if successful, empty string if failed
  std::string AddSpeaker(const std::string& name,
                        const std::vector<std::string>& audio_files,
                        const std::string& gender = "unknown",
                        const std::string& language = "unknown",
                        const std::string& notes = "",
                        bool force = false);

  // 添加说话人声纹（使用预先提取的 embedding）
  // Returns speaker_id if successful, empty string if failed
  std::string AddSpeaker(const std::string& name,
                        const std::vector<float>& embedding,
                        const std::string& gender = "unknown",
                        const std::string& language = "unknown",
                        const std::string& notes = "");

  // 删除说话人
  // Returns true if speaker was found and removed
  bool RemoveSpeaker(const std::string& speaker_id);

  // 重命名说话人
  // Returns true if speaker was found and renamed
  bool RenameSpeaker(const std::string& speaker_id,
                    const std::string& new_name);

  // 识别说话人
  // Returns speaker_id if found, empty string if unknown
  std::string IdentifySpeaker(const std::string& audio_file,
                             float* out_confidence = nullptr);

  // 验证说话人
  // Returns true if confidence >= threshold
  bool VerifySpeaker(const std::string& speaker_id,
                    const std::string& audio_file,
                    float threshold = 0.75f);

  // 列出所有说话人
  std::vector<VoicePrintMetadata> ListSpeakers() const;

  // 获取说话人详细信息
  // Returns nullptr if speaker not found
  std::unique_ptr<VoicePrintMetadata> GetSpeakerInfo(
      const std::string& speaker_id) const;

  // 获取说话人数量
  size_t GetSpeakerCount() const;

  // 导出说话人音频样本到指定目录
  // speaker_id: 要导出的说话人 ID
  // output_dir: 导出目标目录
  // Returns true if all files exported successfully
  bool ExportSpeakerSamples(const std::string& speaker_id,
                            const std::string& output_dir);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// 辅助函数：获取当前时间戳
std::string GetCurrentTimestamp();

// 辅助函数：展开路径（支持 ~）
std::string ExpandPath(const std::string& path);

}  // namespace zasr

#endif  // ZASR_VOICE_PRINT_MANAGER_H
