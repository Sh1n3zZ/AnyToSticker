#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>

#include "include/image_processor.h"

int main(int argc, char* argv[]) {
  try {
    auto args = anysticker::CommandLineArgs::Parse(argc, argv);

    if (args.isBatchMode) {
      // batch mode
      auto results = anysticker::ImageProcessor::ProcessDirectory(
          args.inputPath, args.outputPath, args.options);

      // output processing result statistics
      int successCount = 0;
      for (const auto& result : results) {
        if (result.success) {
          successCount++;
        } else {
          std::cerr << "Processing failed: " << result.inputPath << " - "
                    << result.error << std::endl;
        }
      }

      std::cout << "\nProcessing completed!\n"
                << "Total: " << results.size() << " files\n"
                << "Success: " << successCount << " files\n"
                << "Failed: " << (results.size() - successCount) << " files\n"
                << "Output directory: " << args.outputPath << std::endl;
    } else {
      // check if the input is an animated file
      if (anysticker::ImageProcessor::IsAnimatedImage(args.inputPath)) {
        std::cout << "Detected animated file, extracting the first frame as "
                     "sticker\n";
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

      std::cout << "Processing completed! Output file: " << args.outputPath
                << std::endl;
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error occurred: " << e.what() << std::endl;
    return 1;
  }
}
