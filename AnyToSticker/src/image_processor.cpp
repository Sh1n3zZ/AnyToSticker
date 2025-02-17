#include "../include/image_processor.h"

#include <gif_lib.h>

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

  // convert ext to lowercase
  std::string lowerExt;
  lowerExt.resize(ext.size());
  for (size_t i = 0; i < ext.size(); ++i) {
    lowerExt[i] = std::tolower(static_cast<unsigned char>(ext[i]));
  }

  if (lowerExt == ".gif") {
    return true;  // let every gif be animated
  } else if (lowerExt == ".webp") {
    // read the webp file header to check if it is a dynamic webp
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
  // calc aspect ratio
  double aspectRatio = static_cast<double>(width) / height;

  // the target size: one side must be 512px, the other side is proportional &
  // not greater than 512px
  if (aspectRatio >= 1.0) {
    // when the width is greater than or equal to the height
    width = 512;
    height = static_cast<int>(512 / aspectRatio);
  } else {
    // when the height is greater than the width
    height = 512;
    width = static_cast<int>(512 * aspectRatio);
  }

  std::cout << "Original size: " << width << "x" << height
            << ", aspect ratio: " << aspectRatio << std::endl;

  return cv::Size(width, height);
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
    std::cerr << "Error occurred when saving image: " << e.what() << std::endl;
    return false;
  }
}

// helper function: read the first frame from a gif file
cv::Mat ReadGifFirstFrame(const std::string& path) {
  int error = 0;
  GifFileType* gif = DGifOpenFileName(path.c_str(), &error);
  if (!gif) {
    std::cerr << "Failed to open gif file, error code: " << error << std::endl;
    return cv::Mat();
  }

  if (DGifSlurp(gif) != GIF_OK) {
    std::cerr << "Failed to read gif file, error code: " << gif->Error
              << std::endl;
    DGifCloseFile(gif, &error);
    return cv::Mat();
  }

  // check if there is image data
  if (gif->ImageCount < 1 || !gif->SavedImages) {
    std::cerr << "There is no image data in the gif file" << std::endl;
    DGifCloseFile(gif, &error);
    return cv::Mat();
  }

  // get the first frame information
  SavedImage* image = &gif->SavedImages[0];
  GifImageDesc* desc = &image->ImageDesc;

  // create OpenCV matrix
  cv::Mat result(desc->Height, desc->Width, CV_8UC4);

  // get color map
  ColorMapObject* colorMap =
      gif->SColorMap ? gif->SColorMap : image->ImageDesc.ColorMap;
  if (!colorMap) {
    std::cerr << "There is no color map in the gif file" << std::endl;
    DGifCloseFile(gif, &error);
    return cv::Mat();
  }

  // convert image data
  for (int y = 0; y < desc->Height; y++) {
    for (int x = 0; x < desc->Width; x++) {
      int idx = image->RasterBits[y * desc->Width + x];
      if (idx >= colorMap->ColorCount) {
        idx = 0;
      }
      GifColorType color = colorMap->Colors[idx];

      // BGRA
      result.at<cv::Vec4b>(y, x) = cv::Vec4b(color.Blue, color.Green, color.Red,
                                             255  // fully opaque
      );
    }
  }

  DGifCloseFile(gif, &error);
  return result;
}

bool ImageProcessor::ProcessImage(const std::string& inputPath,
                                  const std::string& outputPath,
                                  const ProcessingOptions& options) {
  try {
    // keep transparent channel read file
    cv::Mat image = cv::imread(inputPath, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
      std::cerr << "Error: cannot read image " << inputPath << std::endl;
      return false;
    }

    // add transparent channel if the image has no transparent channel
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

    // resize
    processedImage = ResizeForTelegram(processedImage);

    // save
    return SaveImage(processedImage, outputPath, options);
  } catch (const std::exception& e) {
    std::cerr << "Error occurred when processing image: " << e.what()
              << std::endl;
    return false;
  }
}

bool ImageProcessor::ProcessAnimation(const std::string& inputPath,
                                      const std::string& outputPath,
                                      const ProcessingOptions& options) {
  try {
    cv::Mat firstFrame;
    std::string errorMsg;

    // use giflib to read gif file
    if (fs::path(inputPath).extension().string() == ".gif" ||
        fs::path(inputPath).extension().string() == ".GIF") {
      std::cout << "Using giflib to read gif file..." << std::endl;
      firstFrame = ReadGifFirstFrame(inputPath);
    } else {
      // use OpenCV to read other formats
      firstFrame = cv::imread(inputPath, cv::IMREAD_UNCHANGED);
    }

    if (firstFrame.empty()) {
      std::cerr << "Error: cannot read file " << inputPath << std::endl;
      std::cerr << "File information: " << std::endl;
      try {
        auto fileStatus = fs::status(inputPath);
        std::cerr << "- File exists: " << fs::exists(inputPath) << std::endl;
        std::cerr << "- File size: " << fs::file_size(inputPath) << " bytes"
                  << std::endl;
        std::cerr << "- File permissions: "
                  << ((fileStatus.permissions() & fs::perms::owner_read) !=
                              fs::perms::none
                          ? "r"
                          : "-")
                  << ((fileStatus.permissions() & fs::perms::owner_write) !=
                              fs::perms::none
                          ? "w"
                          : "-")
                  << ((fileStatus.permissions() & fs::perms::owner_exec) !=
                              fs::perms::none
                          ? "x"
                          : "-")
                  << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "Error occurred when getting file information: "
                  << e.what() << std::endl;
      }
      return false;
    }

    std::cout << "Successfully read image: " << inputPath << std::endl;
    std::cout << "- Size: " << firstFrame.cols << "x" << firstFrame.rows
              << std::endl;
    std::cout << "- Channels: " << firstFrame.channels() << std::endl;
    std::cout << "- Type: " << firstFrame.type() << std::endl;

    // ensure the image has a transparent channel
    cv::Mat processedImage;
    if (firstFrame.channels() == 3) {
      cv::Mat alpha(firstFrame.rows, firstFrame.cols, CV_8UC1, cv::Scalar(255));
      std::vector<cv::Mat> channels;
      cv::split(firstFrame, channels);
      channels.push_back(alpha);
      cv::merge(channels, processedImage);
      std::cout << "Transparent channel added" << std::endl;
    } else {
      processedImage = firstFrame;
    }

    // resize & save
    processedImage = ResizeForTelegram(processedImage);
    std::cout << "Adjusted size: " << processedImage.cols << "x"
              << processedImage.rows << std::endl;

    bool success = SaveImage(processedImage, outputPath, options);
    if (success) {
      std::cout << "Successfully saved to: " << outputPath << std::endl;
    } else {
      std::cerr << "Failed to save: " << outputPath << std::endl;
    }

    return success;
  } catch (const cv::Exception& e) {
    std::cerr << "OpenCV error: " << e.what() << std::endl;
    std::cerr << "Error code: " << e.code << std::endl;
    std::cerr << "Error line: " << e.line << std::endl;
    std::cerr << "Error file: " << e.file << std::endl;
    return false;
  } catch (const std::exception& e) {
    std::cerr << "Error occurred when processing animation: " << e.what()
              << std::endl;
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
    std::cerr << "Error occurred when creating directory: " << e.what()
              << std::endl;
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

      // wildcard matching (supports *.jpg pattern)
      if (pattern == "*") {
        matches.push_back(entry.path());
        continue;
      }

      // check if it is *.ext pattern
      if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
        std::string patternExt = pattern.substr(1);  // get .ext part
        std::string fileExt = entry.path().extension().string();

        // safe extension name to lowercase comparison
        std::string lowerPatternExt, lowerFileExt;
        lowerPatternExt.resize(patternExt.size());
        lowerFileExt.resize(fileExt.size());

        for (size_t i = 0; i < patternExt.size(); ++i) {
          lowerPatternExt[i] =
              std::tolower(static_cast<unsigned char>(patternExt[i]));
        }
        for (size_t i = 0; i < fileExt.size(); ++i) {
          lowerFileExt[i] =
              std::tolower(static_cast<unsigned char>(fileExt[i]));
        }

        if (lowerFileExt == lowerPatternExt) {
          matches.push_back(entry.path());
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Error occurred when searching files: " << e.what()
              << std::endl;
  }

  // sort by file name
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

    // construct output file path
    fs::path outputPath = fs::path(outputDir) / inputPath.filename();
    outputPath.replace_extension(options.format == OutputFormat::WEBP ? ".webp"
                                                                      : ".png");
    result.outputPath = outputPath.string();

    try {
      bool success;
      if (IsAnimatedImage(inputPath.string())) {
        std::cout << "Processing animated file: "
                  << inputPath.filename().string() << std::endl;
        success =
            ProcessAnimation(inputPath.string(), outputPath.string(), options);
      } else {
        std::cout << "Processing image: " << inputPath.filename().string()
                  << std::endl;
        success =
            ProcessImage(inputPath.string(), outputPath.string(), options);
      }

      result.success = success;
      if (!success) {
        result.error = "Processing failed";
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
      << "Usage: AnyToSticker <input path> [options]\n"
      << "The input path can be a single file or a directory\n"
      << "Options:\n"
      << "  -o <output path>   Specify the output file or directory path "
         "(optional)\n"
      << "  --webp             Output in WEBP format (default is PNG)\n"
      << "  -q <quality>       Quality for WEBP format (1-100, default 100)\n"
      << "  -p <pattern>       File matching pattern (e.g., *.jpg, only valid "
         "when processing a directory)\n"
      << "Examples:\n"
      << "  AnyToSticker input.jpg\n"
      << "  AnyToSticker input.gif -o sticker.webp --webp -q 90\n"
      << "  AnyToSticker ./images -o ./stickers --webp -p *.jpg\n";
}

CommandLineArgs CommandLineArgs::Parse(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage();
    throw std::runtime_error("Please provide at least one argument");
  }

  CommandLineArgs args;
  args.inputPath = argv[1];

  // check if the input path is a directory
  args.isBatchMode = fs::is_directory(args.inputPath);

  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      args.outputPath = argv[++i];
    } else if (arg == "--webp") {
      args.options.format = OutputFormat::WEBP;
      // auto change extension name in non-batch mode
      if (!args.isBatchMode && args.outputPath == "output") {
        args.outputPath = "output.webp";
      }
    } else if (arg == "-q" && i + 1 < argc) {
      args.options.quality = std::clamp(std::stoi(argv[++i]), 1, 100);
    } else if (arg == "-p" && i + 1 < argc) {
      args.options.pattern = argv[++i];
    }
  }

  // auto change extension name in non-batch mode
  if (!args.isBatchMode && fs::path(args.outputPath).extension().empty()) {
    args.outputPath +=
        args.options.format == OutputFormat::WEBP ? ".webp" : ".png";
  }

  return args;
}

}  // namespace anysticker
