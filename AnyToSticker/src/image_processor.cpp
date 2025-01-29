#include "../include/image_processor.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <opencv2/opencv.hpp>

#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE
#include <stdio.h>
#endif

namespace fs = std::filesystem;
namespace anysticker {

bool ImageProcessor::IsAnimatedImage(const std::string& path) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".gif") {
    // try to open with VideoCapture
    cv::VideoCapture cap(path);
    if (cap.isOpened()) {
      bool isAnimated = cap.get(cv::CAP_PROP_FRAME_COUNT) > 1;
      cap.release();
      return isAnimated;
    }
    // if VideoCapture failed, try to read with imread
    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
    return !img.empty();  // if can read, treat it as static GIF
  } else if (ext == ".webp") {
    // read WEBP file header to check if it is a dynamic WEBP
    FILE* f = nullptr;
#ifdef _WIN32
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) {
      return false;
    }
#else
    f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
      return false;
    }
#endif

    uint8_t header[16];
    size_t read = fread(header, 1, sizeof(header), f);
    fclose(f);

    if (read < 16) return false;
    return memcmp(header + 12, "WEBP", 4) == 0;
  }

  return false;
}

cv::Size ImageProcessor::CalculateTelegramSize(int width, int height) {
  if (width <= 512 && height <= 512) {
    // 如果两边都小于512，将较长边调整为512
    if (width >= height) {
      double scale = 512.0 / width;
      return cv::Size(512, static_cast<int>(height * scale));
    } else {
      double scale = 512.0 / height;
      return cv::Size(static_cast<int>(width * scale), 512);
    }
  } else {
    // 如果有一边大于512，保持宽高比缩小到符合要求
    double scale = 512.0 / std::max(width, height);
    return cv::Size(static_cast<int>(width * scale),
                    static_cast<int>(height * scale));
  }
}

cv::Mat ImageProcessor::ResizeForTelegram(const cv::Mat& input) {
  cv::Size targetSize = CalculateTelegramSize(input.cols, input.rows);

  cv::Mat output;
  cv::resize(input, output, targetSize, 0, 0, cv::INTER_LANCZOS4);

  return output;
}

bool ImageProcessor::SaveImage(const cv::Mat& image, const std::string& path,
                               const ProcessingOptions& options) {
  std::vector<int> params;
  if (options.format == OutputFormat::WEBP) {
    params = {cv::IMWRITE_WEBP_QUALITY, options.quality};
  } else {
    params = {cv::IMWRITE_PNG_COMPRESSION, 9};
  }

  try {
    return cv::imwrite(path, image, params);
  } catch (const cv::Exception& e) {
    std::cerr << "保存图片时发生错误：" << e.what() << std::endl;
    return false;
  }
}

bool ImageProcessor::ProcessImage(const std::string& inputPath,
                                  const std::string& outputPath,
                                  const ProcessingOptions& options) {
  try {
    // 读取图片（保持透明通道）
    cv::Mat image = cv::imread(inputPath, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
      std::cerr << "错误：无法读取图片 " << inputPath << std::endl;
      return false;
    }

    // 如果图片没有透明通道，添加透明通道
    cv::Mat processedImage;
    if (image.channels() == 3) {
      cv::Mat alpha(image.rows, image.cols, CV_8UC1, cv::Scalar(255));
      std::vector<cv::Mat> channels;
      cv::split(image, channels);
      channels.push_back(alpha);
      cv::merge(channels, processedImage);
    } else {
      processedImage = image;
    }

    // 调整大小
    processedImage = ResizeForTelegram(processedImage);

    // 保存结果
    return SaveImage(processedImage, outputPath, options);
  } catch (const std::exception& e) {
    std::cerr << "处理图片时发生错误：" << e.what() << std::endl;
    return false;
  }
}

bool ImageProcessor::ProcessAnimation(const std::string& inputPath,
                                      const std::string& outputPath,
                                      const ProcessingOptions& options) {
  try {
    cv::VideoCapture cap(inputPath);
    if (!cap.isOpened()) {
      // 如果VideoCapture打开失败，尝试直接用imread读取
      cv::Mat image = cv::imread(inputPath, cv::IMREAD_UNCHANGED);
      if (!image.empty()) {
        return ProcessImage(inputPath, outputPath, options);
      }
      std::cerr << "错误：无法打开动画文件 " << inputPath << std::endl;
      return false;
    }

    // 读取第一帧
    cv::Mat firstFrame;
    if (!cap.read(firstFrame)) {
      std::cerr << "错误：无法读取动画帧 " << std::endl;
      return false;
    }

    // 确保图像有透明通道
    cv::Mat processedImage;
    if (firstFrame.channels() == 3) {
      cv::Mat alpha(firstFrame.rows, firstFrame.cols, CV_8UC1, cv::Scalar(255));
      std::vector<cv::Mat> channels;
      cv::split(firstFrame, channels);
      channels.push_back(alpha);
      cv::merge(channels, processedImage);
    } else {
      processedImage = firstFrame;
    }

    // 调整大小并保存
    processedImage = ResizeForTelegram(processedImage);
    return SaveImage(processedImage, outputPath, options);
  } catch (const std::exception& e) {
    std::cerr << "处理动画时发生错误：" << e.what() << std::endl;
    return false;
  }
}

bool ImageProcessor::EnsureDirectoryExists(const std::string& path) {
  try {
    if (!fs::exists(path)) {
      return fs::create_directories(path);
    }
    return true;
  } catch (const std::exception& e) {
    std::cerr << "创建目录时发生错误：" << e.what() << std::endl;
    return false;
  }
}

std::vector<fs::path> ImageProcessor::GetMatchingFiles(
    const std::string& directory, const std::string& pattern) {
  std::vector<fs::path> matches;

  try {
    for (const auto& entry : fs::directory_iterator(directory)) {
      if (!entry.is_regular_file()) continue;

      std::string filename = entry.path().filename().string();
      // 简单的通配符匹配（支持 *.jpg 这样的模式）
      if (pattern == "*" ||
          (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.' &&
           filename.size() >= pattern.size() - 1 &&
           filename.substr(filename.size() - (pattern.size() - 1)) ==
               pattern.substr(1))) {
        matches.push_back(entry.path());
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "搜索文件时发生错误：" << e.what() << std::endl;
  }

  // 按文件名排序
  std::sort(matches.begin(), matches.end());
  return matches;
}

std::vector<ProcessingResult> ImageProcessor::ProcessDirectory(
    const std::string& inputDir, const std::string& outputDir,
    const ProcessingOptions& options) {
  std::vector<ProcessingResult> results;

  if (!EnsureDirectoryExists(outputDir)) {
    results.push_back({inputDir, outputDir, false, "无法创建输出目录"});
    return results;
  }

  auto files = GetMatchingFiles(inputDir, options.pattern);
  if (files.empty()) {
    results.push_back({inputDir, outputDir, false, "未找到匹配的文件"});
    return results;
  }

  for (const auto& inputPath : files) {
    ProcessingResult result;
    result.inputPath = inputPath.string();

    // 构造输出文件路径
    fs::path outputPath = fs::path(outputDir) / inputPath.filename();
    outputPath.replace_extension(options.format == OutputFormat::WEBP ? ".webp"
                                                                      : ".png");
    result.outputPath = outputPath.string();

    try {
      bool success;
      if (IsAnimatedImage(inputPath.string())) {
        std::cout << "处理动画文件：" << inputPath.filename().string()
                  << std::endl;
        success =
            ProcessAnimation(inputPath.string(), outputPath.string(), options);
      } else {
        std::cout << "处理图片：" << inputPath.filename().string() << std::endl;
        success =
            ProcessImage(inputPath.string(), outputPath.string(), options);
      }

      result.success = success;
      if (!success) {
        result.error = "处理失败";
      }
    } catch (const std::exception& e) {
      result.success = false;
      result.error = e.what();
    }

    results.push_back(result);
  }

  return results;
}

void CommandLineArgs::PrintUsage() {
  std::cout
      << "用法: AnyToSticker <输入路径> [选项]\n"
      << "输入路径可以是单个文件或目录\n"
      << "选项:\n"
      << "  -o <输出路径>   指定输出文件或目录路径（可选）\n"
      << "  --webp          输出为WEBP格式（默认为PNG）\n"
      << "  -q <质量>       WEBP格式的质量（1-100，默认100）\n"
      << "  -p <模式>       文件匹配模式（如 *.jpg，仅在处理目录时有效）\n"
      << "示例:\n"
      << "  AnyToSticker input.jpg\n"
      << "  AnyToSticker input.gif -o sticker.webp --webp -q 90\n"
      << "  AnyToSticker ./images -o ./stickers --webp -p *.jpg\n";
}

CommandLineArgs CommandLineArgs::Parse(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage();
    throw std::runtime_error("参数不足");
  }

  CommandLineArgs args;
  args.inputPath = argv[1];

  // 检查输入路径是否为目录
  args.isBatchMode = fs::is_directory(args.inputPath);

  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      args.outputPath = argv[++i];
    } else if (arg == "--webp") {
      args.options.format = OutputFormat::WEBP;
      // 仅在非批处理模式下自动修改扩展名
      if (!args.isBatchMode && args.outputPath == "output") {
        args.outputPath = "output.webp";
      }
    } else if (arg == "-q" && i + 1 < argc) {
      args.options.quality = std::clamp(std::stoi(argv[++i]), 1, 100);
    } else if (arg == "-p" && i + 1 < argc) {
      args.options.pattern = argv[++i];
    }
  }

  // 在非批处理模式下，如果输出路径没有扩展名，添加默认扩展名
  if (!args.isBatchMode && fs::path(args.outputPath).extension().empty()) {
    args.outputPath +=
        args.options.format == OutputFormat::WEBP ? ".webp" : ".png";
  }

  return args;
}

}  // namespace anysticker
