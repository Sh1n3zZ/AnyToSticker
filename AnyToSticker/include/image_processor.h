#pragma once

#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>

namespace anysticker {

enum class OutputFormat { PNG, WEBP };

struct ProcessingOptions {
  OutputFormat format = OutputFormat::PNG;
  bool preserveAspectRatio = true;
  bool removeBackground = false;
  int quality = 100;          // 仅用于 WEBP 格式
  std::string pattern = "*";  // 文件匹配模式，如 "*.jpg", "*.png" 等
};

struct ProcessingResult {
  std::string inputPath;
  std::string outputPath;
  bool success;
  std::string error;
};

class ImageProcessor {
 public:
  // 检查文件类型
  static bool IsAnimatedImage(const std::string& path);

  // 调整图片大小以符合 Telegram 贴纸要求
  static cv::Mat ResizeForTelegram(const cv::Mat& input);

  // 处理单个文件
  static bool ProcessImage(
      const std::string& inputPath, const std::string& outputPath,
      const ProcessingOptions& options = ProcessingOptions());

  // 处理动图
  static bool ProcessAnimation(
      const std::string& inputPath, const std::string& outputPath,
      const ProcessingOptions& options = ProcessingOptions());

  // 批量处理文件夹
  static std::vector<ProcessingResult> ProcessDirectory(
      const std::string& inputDir, const std::string& outputDir,
      const ProcessingOptions& options = ProcessingOptions());

 private:
  // 计算符合 Telegram 要求的目标尺寸
  static cv::Size CalculateTelegramSize(int width, int height);

  // 保存图片
  static bool SaveImage(const cv::Mat& image, const std::string& path,
                        const ProcessingOptions& options);

  // 确保输出目录存在
  static bool EnsureDirectoryExists(const std::string& path);

  // 获取匹配的文件列表
  static std::vector<std::filesystem::path> GetMatchingFiles(
      const std::string& directory, const std::string& pattern);
};

// 命令行参数结构
struct CommandLineArgs {
  std::string inputPath;
  std::string outputPath = "output";  // 可以是文件或目录
  ProcessingOptions options;
  bool isBatchMode = false;

  static void PrintUsage();
  static CommandLineArgs Parse(int argc, char* argv[]);
};

}  // namespace anysticker
