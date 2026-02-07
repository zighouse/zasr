/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

// 包含标准 C++ 库
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <vector>
#include <string>
#include <map>

// 包含我们的头文件(speaker-identifier.h 已经包含了 sherpa-onnx C API)
#include "speaker-identifier.h"
#include "voice-print-manager.h"
#include "zasr-config.h"

using namespace zasr;

// 打印使用说明
void PrintUsage(const char* program_name) {
  std::cout << "用法:\n";
  std::cout << "  声纹采集:\n";
  std::cout << "    " << program_name << " add --name <Name> --audio <音频文件> [选项]\n";
  std::cout << "    " << program_name << " add --name <Name> --audio <文件1> <文件2> ... [选项]\n\n";
  std::cout << "  声纹管理:\n";
  std::cout << "    " << program_name << " list\n";
  std::cout << "    " << program_name << " info --speaker <说话人ID>\n";
  std::cout << "    " << program_name << " rename --speaker <说话人ID> --name <新Name>\n";
  std::cout << "    " << program_name << " remove --speaker <说话人ID>\n\n";
  std::cout << "  声纹识别:\n";
  std::cout << "    " << program_name << " identify --audio <音频文件>\n\n";
  std::cout << "  声纹验证:\n";
  std::cout << "    " << program_name << " verify --speaker <说话人ID> --audio <音频文件> [--threshold <阈值>]\n\n";
  std::cout << "选项:\n";
  std::cout << "  --model <路径>         Speaker embedding 模型路径\n";
  std::cout << "  --db <路径>            声纹数据库路径(默认：~/.zasr/voice-prints)\n";
  std::cout << "  --threads <N>          线程数(默认：2)\n";
  std::cout << "  --gender <性别>        性别：male/female/unknown(默认：unknown)\n";
  std::cout << "  --language <语言>      语言：zh-CN/en-US/unknown(默认：unknown)\n";
  std::cout << "  --notes <Notes>         自定义Notes\n";
  std::cout << "  --threshold <阈值>     相似度阈值，0-1之间(默认：0.75)\n";
  std::cout << "  --verbose              详细输出\n";
}

// 解析命令行参数
std::string FindArg(const std::vector<const char*>& args, const char* name) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (strcmp(args[i], name) == 0 && i + 1 < args.size()) {
      return args[i + 1];
    }
  }
  return "";
}

bool HasArg(const std::vector<const char*>& args, const char* name) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (strcmp(args[i], name) == 0) {
      return true;
    }
  }
  return false;
}

// 列出所有说话人
int ListSpeakers(VoicePrintManager& manager) {
  auto speakers = manager.ListSpeakers();

  if (speakers.empty()) {
    std::cout << "No registered speakers" << std::endl;
    return 0;
  }

  std::cout << "\nRegistered speakers (" << speakers.size() << "):\n";
  std::cout << std::string(80, '-') << "\n";
  std::cout << std::left << std::setw(15) << "ID"
            << std::setw(20) << "Name"
            << std::setw(20) << "Created At"
            << std::setw(10) << "Samples"
            << "Notes" << "\n";
  std::cout << std::string(80, '-') << "\n";

  for (const auto& speaker : speakers) {
    std::cout << std::left << std::setw(15) << speaker.id
              << std::setw(20) << speaker.name
              << std::setw(20) << speaker.created_at
              << std::setw(10) << speaker.num_samples
              << speaker.metadata.notes << "\n";
  }
  std::cout << std::string(80, '-') << "\n";

  return 0;
}

// 显示Speaker Details
int ShowSpeakerInfo(VoicePrintManager& manager, const std::string& speaker_id) {
  auto metadata = manager.GetSpeakerInfo(speaker_id);
  if (!metadata) {
    std::cerr << "错误：Speaker not found: " << speaker_id << std::endl;
    return 1;
  }

  std::cout << "\nSpeaker Details:\n";
  std::cout << std::string(50, '=') << "\n";
  std::cout << "ID:          " << metadata->id << "\n";
  std::cout << "Name:        " << metadata->name << "\n";
  std::cout << "Created At:    " << metadata->created_at << "\n";
  std::cout << "Updated At:    " << metadata->updated_at << "\n";
  std::cout << "Embedding:   " << metadata->embedding_file
            << " (dim: " << metadata->embedding_dim << ")\n";
  std::cout << "Samples:      " << metadata->num_samples << "\n";
  std::cout << "Gender:        " << metadata->metadata.gender << "\n";
  std::cout << "Language:        " << metadata->metadata.language << "\n";
  std::cout << "Notes:        " << metadata->metadata.notes << "\n";

  if (!metadata->audio_samples.empty()) {
    std::cout << "\nAudio samples:\n";
    for (const auto& sample : metadata->audio_samples) {
      std::cout << "  - " << sample << "\n";
    }
  }

  std::cout << std::string(50, '=') << "\n";

  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::vector<const char*> args(argv, argv + argc);
  std::string command = argv[1];

  // 检查是否需要显示帮助
  if (command == "-h" || command == "--help") {
    PrintUsage(argv[0]);
    return 0;
  }

  // 查找通用参数
  std::string model = FindArg(args, "--model");
  std::string db_path = FindArg(args, "--db");
  std::string threads_str = FindArg(args, "--threads");
  bool verbose = HasArg(args, "--verbose");

   // If没有指定模型，使用默认路径
  if (model.empty()) {
    model = GetDefaultModelPath("speaker-recognition-model/");
    // 检查目录是否存在
    // TODO: 实现更智能的模型查找逻辑
  }

  // 配置管理器
  VoicePrintCollectionConfig config;
  config.model = model;
  config.db_path = db_path;

  if (!threads_str.empty()) {
    config.num_threads = std::stoi(threads_str);
  }

  if (verbose) {
    config.debug = true;
  }

  // Validate configuration
  if (!config.Validate()) {
    return 1;
  }

  // 创建管理器
  VoicePrintManager manager(config);

  if (!manager.Initialize()) {
    std::cerr << "Error: Failed to initialize VoicePrintManager" << std::endl;
    std::cerr << "\nPossible issues:" << std::endl;
    std::cerr << "  1. Model path does not exist: " << config.model << std::endl;
    std::cerr << "  2. Download the speaker recognition model from:" << std::endl;
    std::cerr << "     https://github.com/k2-fsa/sherpa-onnx/releases" << std::endl;
    std::cerr << "\nUse --model <path> to specify the correct model location" << std::endl;
    return 1;
  }

  // 执行命令
  if (command == "list") {
    return ListSpeakers(manager);
  }
  else if (command == "info") {
    std::string speaker_id = FindArg(args, "--speaker");
    if (speaker_id.empty()) {
      std::cerr << "Error: Missing --speaker parameter" << std::endl;
      return 1;
    }
    return ShowSpeakerInfo(manager, speaker_id);
  }
  else if (command == "add") {
    std::string name = FindArg(args, "--name");
    if (name.empty()) {
      std::cerr << "Error: Missing --name parameter" << std::endl;
      return 1;
    }

    // 收集音频文件
    std::vector<std::string> audio_files;
    for (size_t i = 2; i < args.size(); ++i) {
      if (strcmp(args[i], "--audio") == 0 && i + 1 < args.size()) {
        // 检查下一参数是否是选项(以 -- 开头)
        if (strncmp(args[i + 1], "--", 2) != 0) {
          audio_files.push_back(args[i + 1]);
          ++i; // 跳过文件名
        }
      }
    }

    if (audio_files.empty()) {
      std::cerr << "Error: Missing --audio parameter" << std::endl;
      return 1;
    }

    std::string gender = FindArg(args, "--gender");
    if (gender.empty()) gender = "unknown";

    std::string language = FindArg(args, "--language");
    if (language.empty()) language = "unknown";

    std::string notes = FindArg(args, "--notes");

    std::string speaker_id = manager.AddSpeaker(name, audio_files, gender, language, notes);
    if (speaker_id.empty()) {
      std::cerr << "错误：无法添加说话人" << std::endl;
      return 1;
    }

    std::cout << "SUCCESS添加说话人:" << "\n";
    std::cout << "  ID:   " << speaker_id << "\n";
    std::cout << "  Name: " << name << "\n";
    std::cout << "  Samples: " << audio_files.size() << " files\n";

    return 0;
  }
  else if (command == "remove") {
    std::string speaker_id = FindArg(args, "--speaker");
    if (speaker_id.empty()) {
      std::cerr << "Error: Missing --speaker parameter" << std::endl;
      return 1;
    }

    if (!manager.RemoveSpeaker(speaker_id)) {
      std::cerr << "错误：无法删除说话人" << std::endl;
      return 1;
    }

    std::cout << "Successfully removed speaker: " << speaker_id << std::endl;
    return 0;
  }
  else if (command == "rename") {
    std::string speaker_id = FindArg(args, "--speaker");
    std::string new_name = FindArg(args, "--name");

    if (speaker_id.empty() || new_name.empty()) {
      std::cerr << "错误：缺少 --speaker 或 --name 参数" << std::endl;
      return 1;
    }

    if (!manager.RenameSpeaker(speaker_id, new_name)) {
      std::cerr << "错误：无法重命名说话人" << std::endl;
      return 1;
    }

    std::cout << "Successfully renamed speaker: " << speaker_id
              << " -> " << new_name << std::endl;
    return 0;
  }
  else if (command == "identify") {
    std::string audio_file = FindArg(args, "--audio");
    if (audio_file.empty()) {
      std::cerr << "Error: Missing --audio parameter" << std::endl;
      return 1;
    }

    float confidence = 0.0f;
    std::string speaker_id = manager.IdentifySpeaker(audio_file, &confidence);

    if (speaker_id.empty()) {
      std::cout << "No matching speaker found" << std::endl;
      return 0;
    }

    auto metadata = manager.GetSpeakerInfo(speaker_id);
    std::cout << "Identified speaker:" << "\n";
    std::cout << "  ID:        " << speaker_id << "\n";
    std::cout << "  Name:      " << (metadata ? metadata->name : "unknown") << "\n";
    std::cout << "  Confidence:    " << std::fixed << std::setprecision(2)
              << (confidence * 100.0f) << "%" << std::endl;

    return 0;
  }
  else if (command == "verify") {
    std::string speaker_id = FindArg(args, "--speaker");
    std::string audio_file = FindArg(args, "--audio");
    std::string threshold_str = FindArg(args, "--threshold");

    if (speaker_id.empty() || audio_file.empty()) {
      std::cerr << "错误：缺少 --speaker 或 --audio 参数" << std::endl;
      return 1;
    }

    float threshold = 0.75f;
    if (!threshold_str.empty()) {
      threshold = std::stof(threshold_str);
    }

    bool verified = manager.VerifySpeaker(speaker_id, audio_file, threshold);

    auto metadata = manager.GetSpeakerInfo(speaker_id);
    std::cout << "Verify speaker: " << (metadata ? metadata->name : speaker_id) << "\n";
    std::cout << "  Audio file:  " << audio_file << "\n";
    std::cout << "  Threshold:      " << std::fixed << std::setprecision(2)
              << threshold << "\n";
    std::cout << "  Result:      " << (verified ? "PASS" : "✗ FAILED") << std::endl;

    return verified ? 0 : 1;
  }
  else {
    std::cerr << "错误：unknown命令 '" << command << "'" << std::endl;
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
