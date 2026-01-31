#include "yaml-config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <dirent.h>
#include <pwd.h>

// Include yaml-cpp headers
#include <yaml-cpp/yaml.h>

namespace zasr {

bool YamlConfig::LoadFromFile(const std::string& filepath) {
  try {
    // Read file content into string
    std::ifstream file(filepath);
    if (!file.is_open()) {
      error_ = "Cannot open file: " + filepath;
      return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    yaml_content_ = buffer.str();

    // Validate YAML syntax
    YAML::Load(yaml_content_);
    return true;
  } catch (const YAML::Exception& e) {
    error_ = std::string("YAML parse error: ") + e.what();
    return false;
  } catch (const std::exception& e) {
    error_ = std::string("Error reading file: ") + e.what();
    return false;
  }
}

std::string YamlConfig::GetString(const std::string& key, const std::string& default_value) const {
  if (yaml_content_.empty()) {
    return default_value;
  }

  try {
    // Parse YAML content on each access
    YAML::Node root = YAML::Load(yaml_content_);

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    // Traverse the node hierarchy
    YAML::Node current = root;
    for (const auto& p : parts) {
      if (!current.IsMap() || !current[p].IsDefined()) {
        return default_value;
      }
      current = current[p];
    }

    if (current.IsScalar()) {
      std::string value = current.as<std::string>();
      // Expand environment variables
      return ExpandEnvVars(value);
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

int YamlConfig::GetInt(const std::string& key, int default_value) const {
  if (yaml_content_.empty()) {
    return default_value;
  }

  try {
    YAML::Node root = YAML::Load(yaml_content_);

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    // Traverse the node hierarchy
    YAML::Node current = root;
    for (const auto& p : parts) {
      if (!current.IsMap() || !current[p]) {
        return default_value;
      }
      current = current[p];
    }

    if (current.IsScalar()) {
      return current.as<int>();
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

float YamlConfig::GetFloat(const std::string& key, float default_value) const {
  if (yaml_content_.empty()) {
    return default_value;
  }

  try {
    YAML::Node root = YAML::Load(yaml_content_);

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    // Traverse the node hierarchy
    YAML::Node current = root;
    for (const auto& p : parts) {
      if (!current.IsMap() || !current[p]) {
        return default_value;
      }
      current = current[p];
    }

    if (current.IsScalar()) {
      return current.as<float>();
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

bool YamlConfig::GetBool(const std::string& key, bool default_value) const {
  if (yaml_content_.empty()) {
    return default_value;
  }

  try {
    YAML::Node root = YAML::Load(yaml_content_);

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    // Traverse the node hierarchy
    YAML::Node current = root;
    for (const auto& p : parts) {
      if (!current.IsMap() || !current[p]) {
        return default_value;
      }
      current = current[p];
    }

    if (current.IsScalar()) {
      return current.as<bool>();
    }
  } catch (const YAML::Exception& e) {
    // Return default value on any error
  }

  return default_value;
}

std::vector<std::string> YamlConfig::GetStringList(const std::string& key) const {
  std::vector<std::string> result;

  if (yaml_content_.empty()) {
    return result;
  }

  try {
    YAML::Node root = YAML::Load(yaml_content_);

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    // Use a copy of the node to traverse
    YAML::Node current = root;
    for (const auto& p : parts) {
      if (!current.IsMap() || !current[p]) {
        return result;
      }
      current = current[p];
    }

    if (current.IsSequence()) {
      for (const auto& item : current) {
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
  if (yaml_content_.empty()) {
    return false;
  }

  try {
    YAML::Node root = YAML::Load(yaml_content_);

    // Support nested keys with "." separator
    std::vector<std::string> parts;
    std::stringstream ss(key);
    std::string part;
    while (std::getline(ss, part, '.')) {
      parts.push_back(part);
    }

    // Traverse the node hierarchy
    YAML::Node current = root;
    for (const auto& p : parts) {
      if (!current.IsMap() || !current[p]) {
        return false;
      }
      current = current[p];
    }

    return true;
  } catch (const YAML::Exception& e) {
    return false;
  }
}

std::string YamlConfig::ExpandEnvVars(const std::string& str) {
  std::string result = str;

  // First, expand ~ to home directory
  size_t tilde_pos = 0;
  while ((tilde_pos = result.find('~', tilde_pos)) != std::string::npos) {
    // Check if it's really a home directory reference (~ or ~/path)
    // Skip if it's part of a larger word (like http://)
    if (tilde_pos > 0 &&
        (std::isalnum(static_cast<unsigned char>(result[tilde_pos - 1])) ||
         result[tilde_pos - 1] == '_' ||
         result[tilde_pos - 1] == '/')) {
      tilde_pos++;
      continue;
    }

    // Check if ~ is at start or followed by /
    if (tilde_pos == 0 || result[tilde_pos + 1] == '/') {
      const char* home = getenv("HOME");
      if (home) {
        size_t home_len = strlen(home);
        if (tilde_pos + 1 < result.length() && result[tilde_pos + 1] == '/') {
          // ~/path -> /home/user/path
          result.replace(tilde_pos, 1, home);
          tilde_pos += home_len;
        } else {
          // ~ -> /home/user
          result.replace(tilde_pos, 1, home);
          tilde_pos += home_len;
        }
      } else {
        // HOME not set, keep ~
        tilde_pos++;
      }
    } else {
      tilde_pos++;
    }
  }

  // Then expand ${VAR} style
  size_t pos = 0;
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

  // If not found, try recursive search in default paths
  for (const auto& search_path : search_paths) {
    std::string result = FindFileRecursive(search_path, filename);
    if (!result.empty()) {
      return result;
    }
  }

  return "";
}

// Helper function for recursive file search
std::string YamlConfig::FindFileRecursive(const std::string& base_path,
                                          const std::string& filename) {
  // First, try direct path
  std::string direct_path = base_path + "/" + filename;
  struct stat buffer;
  if (stat(direct_path.c_str(), &buffer) == 0) {
    return direct_path;
  }

  // Try as subdirectory path (e.g., "sense-voice/model.onnx")
  DIR* dir = opendir(base_path.c_str());
  if (!dir) {
    return "";
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      std::string sub_path = base_path + "/" + entry->d_name;
      std::string result = FindFileRecursive(sub_path, filename);
      if (!result.empty()) {
        closedir(dir);
        return result;
      }
    }
  }

  closedir(dir);
  return "";
}

// Find file in a specific model directory
std::string YamlConfig::FindFileInModelDir(const std::vector<std::string>& search_paths,
                                          const std::string& model_dir,
                                          const std::string& filename) {
  // Search in each base path for a subdirectory matching model_dir
  for (const auto& base_path : search_paths) {
    DIR* dir = opendir(base_path.c_str());
    if (!dir) {
      continue;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
        std::string subdir_name = entry->d_name;
        // Check if this subdirectory matches or contains the model_dir name
        if (subdir_name.find(model_dir) != std::string::npos) {
          // Found matching directory, check for the file
          std::string full_path = base_path + "/" + subdir_name + "/" + filename;
          struct stat buffer;
          if (stat(full_path.c_str(), &buffer) == 0) {
            closedir(dir);
            return full_path;
          }
        }
      }
    }

    closedir(dir);
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
