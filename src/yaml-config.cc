#include "yaml-config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>

// Forward declare and include yaml-cpp
namespace YAML {
class Node;
}

// Include yaml-cpp headers
#include <yaml-cpp/yaml.h>

namespace zasr {

// Wrapper class to hold YAML::Node without including yaml-cpp in header
class YamlNodeWrapper {
public:
  YamlNodeWrapper() : node_(std::make_unique<YAML::Node>()) {}
  explicit YamlNodeWrapper(const YAML::Node& node) : node_(std::make_unique<YAML::Node>(node)) {}

  YAML::Node* get() { return node_.get(); }
  const YAML::Node* get() const { return node_.get(); }

private:
  std::unique_ptr<YAML::Node> node_;
};

bool YamlConfig::LoadFromFile(const std::string& filepath) {
  try {
    auto root = std::make_unique<YamlNodeWrapper>(YAML::LoadFile(filepath));

    yaml_root_ = root.release();
    return true;
  } catch (const YAML::Exception& e) {
    error_ = std::string("YAML parse error: ") + e.what();
    return false;
  }
}

std::string YamlConfig::GetString(const std::string& key, const std::string& default_value) const {
  if (!yaml_root_) {
    return default_value;
  }

  try {
    const YAML::Node* root = static_cast<YamlNodeWrapper*>(yaml_root_)->get();

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    const YAML::Node* current = root;
    for (const auto& p : parts) {
      if (!current->IsMap() || !(*current)[p]) {
        return default_value;
      }
      current = &((*current)[p]);
    }

    if (current->IsScalar()) {
      std::string value = current->as<std::string>();
      // Expand environment variables
      return ExpandEnvVars(value);
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

int YamlConfig::GetInt(const std::string& key, int default_value) const {
  if (!yaml_root_) {
    return default_value;
  }

  try {
    const YAML::Node* root = static_cast<YamlNodeWrapper*>(yaml_root_)->get();

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    const YAML::Node* current = root;
    for (const auto& p : parts) {
      if (!current->IsMap() || !(*current)[p]) {
        return default_value;
      }
      current = &((*current)[p]);
    }

    if (current->IsScalar()) {
      return current->as<int>();
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

float YamlConfig::GetFloat(const std::string& key, float default_value) const {
  if (!yaml_root_) {
    return default_value;
  }

  try {
    const YAML::Node* root = static_cast<YamlNodeWrapper*>(yaml_root_)->get();

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    const YAML::Node* current = root;
    for (const auto& p : parts) {
      if (!current->IsMap() || !(*current)[p]) {
        return default_value;
      }
      current = &((*current)[p]);
    }

    if (current->IsScalar()) {
      return current->as<float>();
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

bool YamlConfig::GetBool(const std::string& key, bool default_value) const {
  if (!yaml_root_) {
    return default_value;
  }

  try {
    const YAML::Node* root = static_cast<YamlNodeWrapper*>(yaml_root_)->get();

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    const YAML::Node* current = root;
    for (const auto& p : parts) {
      if (!current->IsMap() || !(*current)[p]) {
        return default_value;
      }
      current = &((*current)[p]);
    }

    if (current->IsScalar()) {
      return current->as<bool>();
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

std::vector<std::string> YamlConfig::GetStringList(const std::string& key) const {
  std::vector<std::string> result;

  if (!yaml_root_) {
    return result;
  }

  try {
    const YAML::Node* root = static_cast<YamlNodeWrapper*>(yaml_root_)->get();

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    const YAML::Node* current = root;
    for (const auto& p : parts) {
      if (!current->IsMap() || !(*current)[p]) {
        return result;
      }
      current = &((*current)[p]);
    }

    if (current->IsSequence()) {
      for (const auto& item : *current) {
        if (item.IsScalar()) {
          std::string value = item.as<std::string>();
          // Expand environment variables
          result.push_back(ExpandEnvVars(value));
        }
      }
    }
  } catch (const YAML::Exception& e) {
    // Return empty vector on any error
  }

  return result;
}

bool YamlConfig::HasKey(const std::string& key) const {
  if (!yaml_root_) {
    return false;
  }

  try {
    const YAML::Node* root = static_cast<YamlNodeWrapper*>(yaml_root_)->get();

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    const YAML::Node* current = root;
    for (const auto& p : parts) {
      if (!current->IsMap() || !(*current)[p]) {
        return false;
      }
      current = &((*current)[p]);
    }

    return true;
  } catch (const YAML::Exception& e) {
    return false;
  }
}

YamlConfig::~YamlConfig() {
  if (yaml_root_) {
    delete static_cast<YamlNodeWrapper*>(yaml_root_);
    yaml_root_ = nullptr;
  }
}

std::string YamlConfig::ExpandEnvVars(const std::string& str) {
  std::string result = str;
  size_t pos = 0;

  // Expand ${VAR} style
  while ((pos = result.find("${", pos)) != std::string::npos) {
    size_t end = result.find('}', pos);
    if (end == std::string::npos) {
      break;
    }

    std::string var_name = result.substr(pos + 2, end - pos - 2);
    const char* var_value = std::getenv(var_name.c_str());

    if (var_value) {
      result.replace(pos, end - pos + 1, var_value);
      pos += strlen(var_value);
    } else {
      // Keep original if env var not set
      pos = end + 1;
    }
  }

  // Expand $VAR style (only if not followed by alphanumeric or underscore)
  pos = 0;
  while ((pos = result.find('$', pos)) != std::string::npos) {
    // Skip if it's already processed as ${VAR}
    if (pos + 1 < result.length() && result[pos + 1] == '{') {
      pos++;
      continue;
    }

    size_t start = pos + 1;
    size_t end = start;
    while (end < result.length() &&
           (std::isalnum(static_cast<unsigned char>(result[end])) || result[end] == '_')) {
      end++;
    }

    if (end > start) {
      std::string var_name = result.substr(start, end - start);
      const char* var_value = std::getenv(var_name.c_str());

      if (var_value) {
        result.replace(pos, end - pos, var_value);
        pos += strlen(var_value);
      } else {
        pos = end;
      }
    } else {
      pos++;
    }
  }

  return result;
}

std::string YamlConfig::FindFileInPaths(const std::string& filename,
                                         const std::vector<std::string>& search_paths) {
  // First, check if it's an absolute path
  if (!filename.empty() && filename[0] == '/') {
    struct stat buffer;
    if (stat(filename.c_str(), &buffer) == 0) {
      return filename;
    }
    return "";
  }

  // Search in each path
  for (const auto& path : search_paths) {
    std::string full_path = path + "/" + filename;
    struct stat buffer;
    if (stat(full_path.c_str(), &buffer) == 0) {
      return full_path;
    }
  }

  return "";
}

std::vector<std::string> YamlConfig::GetDefaultConfigPaths() {
  std::vector<std::string> paths;

  // 1. Deploy directory (if DEPLOY_DIR is set)
  const char* deploy_dir = std::getenv("DEPLOY_DIR");
  if (deploy_dir) {
    paths.push_back(std::string(deploy_dir) + "/config");
  }

  // 2. User config directory
  const char* home = std::getenv("HOME");
  if (home) {
    paths.push_back(std::string(home) + "/.config/zasr");
  }

  // 3. System config directory
  paths.push_back("/etc/zasr");

  return paths;
}

std::vector<std::string> YamlConfig::GetDefaultModelPaths() {
  std::vector<std::string> paths;

  // 1. Deploy directory models (if DEPLOY_DIR is set)
  const char* deploy_dir = std::getenv("DEPLOY_DIR");
  if (deploy_dir) {
    paths.push_back(std::string(deploy_dir) + "/models");
  }

  // 2. Sherpa-ONNX cache directory
  const char* home = std::getenv("HOME");
  if (home) {
    paths.push_back(std::string(home) + "/.cache/sherpa-onnx");
  }

  // 3. System-wide models directory
  paths.push_back("/usr/local/share/sherpa-onnx");

  return paths;
}

}  // namespace zasr
