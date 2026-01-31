#ifndef ZASR_YAML_CONFIG_H
#define ZASR_YAML_CONFIG_H

#include <string>
#include <vector>

namespace zasr {

// YamlConfig - YAML configuration parser helper
// Provides environment variable expansion and path resolution
class YamlConfig {
public:
  // Load YAML from file
  // Returns true if successful, false on error
  bool LoadFromFile(const std::string& filepath);

  // Get string value by key (supports nested keys with ".")
  // Returns empty string if key doesn't exist
  std::string GetString(const std::string& key, const std::string& default_value = "") const;

  // Get integer value by key
  // Returns default_value if key doesn't exist
  int GetInt(const std::string& key, int default_value = 0) const;

  // Get float value by key
  // Returns default_value if key doesn't exist
  float GetFloat(const std::string& key, float default_value = 0.0f) const;

  // Get boolean value by key
  // Returns default_value if key doesn't exist
  bool GetBool(const std::string& key, bool default_value = false) const;

  // Get string list value by key
  // Returns empty vector if key doesn't exist
  std::vector<std::string> GetStringList(const std::string& key) const;

  // Check if key exists
  bool HasKey(const std::string& key) const;

  // Get last error message
  std::string GetError() const { return error_; }

  // Expand environment variables in string
  // Replaces ${VAR} and $VAR with environment variable values
  static std::string ExpandEnvVars(const std::string& str);

  // Find file in search paths
  // Returns first existing file path, or empty string if not found
  static std::string FindFileInPaths(const std::string& filename,
                                      const std::vector<std::string>& search_paths);

  // Find file in a specific model directory
  // Searches for filename in subdirectories that contain model_dir as part of their path
  static std::string FindFileInModelDir(const std::vector<std::string>& search_paths,
                                        const std::string& model_dir,
                                        const std::string& filename);

  // Get default configuration search paths
  static std::vector<std::string> GetDefaultConfigPaths();

  // Get default model search paths
  static std::vector<std::string> GetDefaultModelPaths();

  ~YamlConfig() = default;

private:
  // Helper function for recursive file search
  static std::string FindFileRecursive(const std::string& base_path,
                                       const std::string& filename);

  std::string error_;
  std::string yaml_content_;  // YAML content as string (reparsed on each access)
};

}  // namespace zasr

#endif  // ZASR_YAML_CONFIG_H
