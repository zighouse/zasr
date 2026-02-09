/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#ifndef ZASR_SPEAKER_IDENTIFIER_H
#define ZASR_SPEAKER_IDENTIFIER_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <stdexcept>
#include <cstdint>

// 重要：必须最先包含 sherpa-onnx C API，避免头文件冲突
extern "C" {
#include "sherpa-onnx/c-api/c-api.h"
}

#include "voice-print-db.h"

namespace zasr {

// RAII 封装用于 SherpaOnnxWave
class SherpaWaveWrapper {
 public:
  explicit SherpaWaveWrapper(const std::string& wav_path)
      : wave_(const_cast<SherpaOnnxWave*>(SherpaOnnxReadWave(wav_path.c_str()))) {
    if (!wave_) {
      throw std::runtime_error("Failed to read wave file: " + wav_path);
    }
  }

  ~SherpaWaveWrapper() {
    if (wave_) {
      SherpaOnnxFreeWave(const_cast<const SherpaOnnxWave*>(wave_));
    }
  }

  // 禁止拷贝
  SherpaWaveWrapper(const SherpaWaveWrapper&) = delete;
  SherpaWaveWrapper& operator=(const SherpaWaveWrapper&) = delete;

  const SherpaOnnxWave* Get() const { return wave_; }

  int32_t GetSampleRate() const { return wave_->sample_rate; }
  const float* GetSamples() const { return wave_->samples; }
  int32_t GetNumSamples() const { return wave_->num_samples; }

 private:
  SherpaOnnxWave* wave_;
};

// RAII 封装用于 SherpaOnnxOnlineStream
class SherpaStreamWrapper {
 public:
  explicit SherpaStreamWrapper(const SherpaOnnxSpeakerEmbeddingExtractor* extractor)
      : stream_(const_cast<SherpaOnnxOnlineStream*>(
            SherpaOnnxSpeakerEmbeddingExtractorCreateStream(extractor))) {
    if (!stream_) {
      throw std::runtime_error("Failed to create stream");
    }
  }

  ~SherpaStreamWrapper() {
    if (stream_) {
      SherpaOnnxDestroyOnlineStream(const_cast<const SherpaOnnxOnlineStream*>(stream_));
    }
  }

  // 禁止拷贝
  SherpaStreamWrapper(const SherpaStreamWrapper&) = delete;
  SherpaStreamWrapper& operator=(const SherpaStreamWrapper&) = delete;

  const SherpaOnnxOnlineStream* Get() const { return stream_; }

 private:
  SherpaOnnxOnlineStream* stream_;
};

// 自定义 deleter 用于 sherpa-onnx C API 指针
struct SherpaExtractorDeleter {
  void operator()(SherpaOnnxSpeakerEmbeddingExtractor* ptr) const {
    SherpaOnnxDestroySpeakerEmbeddingExtractor(const_cast<const SherpaOnnxSpeakerEmbeddingExtractor*>(ptr));
  }
};

struct SherpaManagerDeleter {
  void operator()(SherpaOnnxSpeakerEmbeddingManager* ptr) const {
    SherpaOnnxDestroySpeakerEmbeddingManager(const_cast<const SherpaOnnxSpeakerEmbeddingManager*>(ptr));
  }
};

// 说话人识别器 - 使用 sherpa-onnx C API
class ZSpeakerIdentifier {
 public:
  struct Config {
    std::string model;              // sherpa-onnx speaker embedding 模型路径
    int32_t num_threads = 2;        // 线程数
    bool debug = false;
    std::string provider = "cpu";
    std::string voice_print_db;    // 声纹数据库路径
    float similarity_threshold = 0.75f;   // 相似度阈值
    bool enable_auto_track = true;        // 是否自动跟踪新说话人
  };

  struct SpeakerMatch {
    std::string name;      // 说话人姓名
    std::string id;        // 说话人 ID
    float score = 0.0f;    // 相似度分数 [0, 1]
  };

  struct IdentificationResult {
    std::string speaker_id;        // 说话人 ID
    std::string speaker_name;      // 说话人姓名
    float confidence = 0.0f;       // 置信度 [0, 1]
    bool is_new_speaker = false;   // 是否是新检测到的说话人

    // Top N 最佳匹配（按相似度排序）
    std::vector<SpeakerMatch> top_matches;
  };

  // 构造函数
  explicit ZSpeakerIdentifier(const Config& config);

  // 析构函数
  ~ZSpeakerIdentifier();

  // 禁止拷贝和赋值
  ZSpeakerIdentifier(const ZSpeakerIdentifier&) = delete;
  ZSpeakerIdentifier& operator=(const ZSpeakerIdentifier&) = delete;

  // 初始化（加载声纹数据库）
  bool Initialize();

  // 处理音频片段，返回识别结果
  IdentificationResult ProcessSegment(const std::vector<float>& samples);

  // 从 WAV 文件识别说话人
  IdentificationResult IdentifyFromWav(const std::string& wav_path);

  // 添加说话人（运行时动态添加），返回 speaker_id，失败返回空字符串
  // force: 如果为 true，跳过多说话人检测；如果为 false，检测到多说话人时返回空字符串
  std::string AddSpeaker(const std::string& name, const std::vector<std::string>& wav_files,
                        bool force = false);

  // 获取当前已注册的说话人数量
  size_t GetRegisteredSpeakerCount() const {
    return manager_ ? SherpaOnnxSpeakerEmbeddingManagerNumSpeakers(
        const_cast<const SherpaOnnxSpeakerEmbeddingManager*>(manager_.get())) : 0;
  }

  // 检查是否已初始化
  bool IsInitialized() const { return initialized_; }

  // 获取配置
  const Config& GetConfig() const { return config_; }

  // 获取 embedding 维度
  int32_t GetEmbeddingDim() const {
    return extractor_ ? SherpaOnnxSpeakerEmbeddingExtractorDim(
        const_cast<const SherpaOnnxSpeakerEmbeddingExtractor*>(extractor_.get())) : 0;
  }

  // 检测音频文件中的说话人数量
  // 返回说话人数量，失败返回 -1
  int DetectNumSpeakers(const std::string& wav_path);

 private:
  // 从音频提取 embedding
  std::vector<float> ExtractEmbedding(const std::vector<float>& samples);

  // 从 WAV 文件提取 embedding
  std::vector<float> ExtractEmbeddingFromWav(const std::string& wav_path);

  // 比较 embedding 并找到最佳匹配
  IdentificationResult MatchSpeaker(const std::vector<float>& embedding);

  // 创建新说话人记录
  std::string CreateUnknownSpeaker(const std::vector<float>& embedding);

  // 验证说话人
  bool VerifySpeaker(const std::string& name, const std::vector<float>& embedding);

 private:
  Config config_;

  // sherpa-onnx C API 对象（使用自定义 deleter）
  std::unique_ptr<SherpaOnnxSpeakerEmbeddingExtractor, SherpaExtractorDeleter> extractor_;
  std::unique_ptr<SherpaOnnxSpeakerEmbeddingManager, SherpaManagerDeleter> manager_;

  // 声纹数据库
  std::unique_ptr<VoicePrintDatabase> database_;

  bool initialized_ = false;
};

}  // namespace zasr

#endif  // ZASR_SPEAKER_IDENTIFIER_H
