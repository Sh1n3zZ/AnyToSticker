#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>

#include "include/image_processor.h"

int main(int argc, char* argv[]) {
  try {
    auto args = anysticker::CommandLineArgs::Parse(argc, argv);

    if (args.isBatchMode) {
      // 批量处理模式
      auto results = anysticker::ImageProcessor::ProcessDirectory(
          args.inputPath, args.outputPath, args.options);

      // 输出处理结果统计
      int successCount = 0;
      for (const auto& result : results) {
        if (result.success) {
          successCount++;
        } else {
          std::cerr << "处理失败：" << result.inputPath << " - " << result.error
                    << std::endl;
        }
      }

      std::cout << "\n处理完成！\n"
                << "总计：" << results.size() << " 个文件\n"
                << "成功：" << successCount << " 个\n"
                << "失败：" << (results.size() - successCount) << " 个\n"
                << "输出目录：" << args.outputPath << std::endl;
    } else {
      // 检查是否为动画文件
      if (anysticker::ImageProcessor::IsAnimatedImage(args.inputPath)) {
        std::cout << "检测到动画文件，将提取第一帧作为贴纸\n";
        if (!anysticker::ImageProcessor::ProcessAnimation(
                args.inputPath, args.outputPath, args.options)) {
          return 1;
        }
      } else {
        if (!anysticker::ImageProcessor::ProcessImage(
                args.inputPath, args.outputPath, args.options)) {
          return 1;
        }
      }

      std::cout << "处理完成！输出文件：" << args.outputPath << std::endl;
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "错误：" << e.what() << std::endl;
    return 1;
  }
}
