#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "presspng/compressor.h"
#include "presspng/format_bytes.h"
#include "presspng/preview.h"

namespace {

void printUsage() {
    std::cout <<
        "PressPNG command line interface\n"
        "\n"
        "Usage:\n"
        "  presspng-cli <image> [options]\n"
        "\n"
        "Options:\n"
        "  -s, --size <N>[KB|MB]   target file size (default unit: KB)\n"
        "  -f, --format <fmt>      keep | jpeg | png | qoi | ppm   (default: keep)\n"
        "  -o, --output <dir>      output directory (default: next to input)\n"
        "      --resize <WxH>      cap output dimensions (never upscales)\n"
        "      --crop <x,y,w,h>    crop as fractions of the image (0..1)\n"
        "      --rotate <90|180|270>   rotate clockwise\n"
        "      --flip <h|v|both>   mirror horizontally / vertically\n"
        "  -q, --quiet             no progress output\n"
        "  -h, --help              show this help\n"
        "\n"
        "Exit codes: 0 = target met, 2 = written but target not met, 1 = error\n";
}

bool parseSize(const std::string& text, std::uint64_t& bytes) {
    if (text.empty()) {
        return false;
    }
    std::size_t digits = 0;
    while (digits < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[digits])) != 0)) {
        ++digits;
    }
    if (digits == 0) {
        return false;
    }
    const std::uint64_t value = std::strtoull(text.c_str(), nullptr, 10);
    if (value == 0) {
        return false;
    }
    std::string unit = text.substr(digits);
    for (char& c : unit) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (unit.empty() || unit == "KB" || unit == "K") {
        bytes = value * 1000ULL;
    } else if (unit == "MB" || unit == "M") {
        bytes = value * 1000000ULL;
    } else if (unit == "B") {
        bytes = value;
    } else {
        return false;
    }
    return true;
}

// Splits on a single separator and parses each part as a double.
bool parseDoubles(const std::string& text, char separator,
                  std::vector<double>& out, std::size_t expected) {
    out.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        const std::string part =
            text.substr(start, end == std::string::npos ? std::string::npos
                                                        : end - start);
        if (part.empty()) {
            return false;
        }
        char* stop = nullptr;
        const double value = std::strtod(part.c_str(), &stop);
        if (stop == part.c_str() || *stop != '\0') {
            return false;
        }
        out.push_back(value);
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out.size() == expected;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> inputs;
    std::string formatText = "keep";
    std::string outputDir;
    std::uint64_t targetBytes = 0;
    bool quiet = false;
    presspng::TransformSpec transform;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "-q" || arg == "--quiet") {
            quiet = true;
        } else if (arg == "-s" || arg == "--size") {
            if (i + 1 >= argc || !parseSize(argv[++i], targetBytes)) {
                std::cerr << "error: bad --size value\n";
                return 1;
            }
        } else if (arg == "-f" || arg == "--format") {
            if (i + 1 >= argc) {
                std::cerr << "error: --format needs a value\n";
                return 1;
            }
            formatText = argv[++i];
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "error: --output needs a value\n";
                return 1;
            }
            outputDir = argv[++i];
        } else if (arg == "--resize") {
            std::vector<double> parts;
            if (i + 1 >= argc || !parseDoubles(argv[++i], 'x', parts, 2)) {
                std::cerr << "error: --resize expects WxH (e.g. 1920x1080)\n";
                return 1;
            }
            transform.outputWidth = static_cast<int>(parts[0]);
            transform.outputHeight = static_cast<int>(parts[1]);
        } else if (arg == "--crop") {
            std::vector<double> parts;
            if (i + 1 >= argc || !parseDoubles(argv[++i], ',', parts, 4)) {
                std::cerr << "error: --crop expects x,y,w,h fractions\n";
                return 1;
            }
            transform.cropX = parts[0];
            transform.cropY = parts[1];
            transform.cropW = parts[2];
            transform.cropH = parts[3];
        } else if (arg == "--rotate") {
            if (i + 1 >= argc) {
                std::cerr << "error: --rotate needs an angle\n";
                return 1;
            }
            const std::string angle = argv[++i];
            if (angle == "90") {
                transform.rotate = presspng::Rotate::Cw90;
            } else if (angle == "180") {
                transform.rotate = presspng::Rotate::Cw180;
            } else if (angle == "270") {
                transform.rotate = presspng::Rotate::Cw270;
            } else {
                std::cerr << "error: --rotate must be 90, 180 or 270\n";
                return 1;
            }
        } else if (arg == "--flip") {
            if (i + 1 >= argc) {
                std::cerr << "error: --flip needs h, v or both\n";
                return 1;
            }
            const std::string axis = argv[++i];
            if (axis == "h") {
                transform.flipH = true;
            } else if (axis == "v") {
                transform.flipV = true;
            } else if (axis == "both") {
                transform.flipH = true;
                transform.flipV = true;
            } else {
                std::cerr << "error: --flip must be h, v or both\n";
                return 1;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option " << arg << "\n";
            return 1;
        } else {
            inputs.push_back(arg);
        }
    }

    if (inputs.empty()) {
        printUsage();
        return 1;
    }
    if (targetBytes == 0) {
        std::cerr << "error: --size is required (e.g. -s 500KB)\n";
        return 1;
    }

    presspng::UserFormat format = presspng::UserFormat::Keep;
    if (formatText == "jpeg" || formatText == "jpg") {
        format = presspng::UserFormat::Jpeg;
    } else if (formatText == "png") {
        format = presspng::UserFormat::Png;
    } else if (formatText == "qoi") {
        format = presspng::UserFormat::Qoi;
    } else if (formatText == "ppm") {
        format = presspng::UserFormat::Ppm;
    } else if (formatText != "keep") {
        std::cerr << "error: unknown format " << formatText << "\n";
        return 1;
    }

    presspng::CompressionRequest request;
    request.input = inputs.empty() ? "" : inputs[0];
    request.outputDirectory = outputDir;
    request.targetBytes = targetBytes;
    request.format = format;
    request.transform = transform;

    presspng::Compressor compressor;
    bool anyNotMet = false;
    for (const auto& inPath : inputs) {
        request.input = inPath;
        const presspng::CompressionResult result = compressor.compress(
            request, quiet ? presspng::Compressor::ProgressFn{}
                           : presspng::Compressor::ProgressFn{
                                 [](const std::string& stage) {
                                     std::cout << "  ... " << stage << "\n";
                                 }});

        if (!result.ok) {
            std::cerr << inPath << ": error: " << result.message << "\n";
            if (inputs.size() == 1) return 1;
            continue;
        }

        const char* formatName = "?";
        switch (result.encodedFormat) {
            case presspng::EncodedFormat::Jpeg: formatName = "JPEG"; break;
            case presspng::EncodedFormat::Png:  formatName = "PNG";  break;
            case presspng::EncodedFormat::Bmp:  formatName = "BMP";  break;
            case presspng::EncodedFormat::Tga:  formatName = "TGA";  break;
            case presspng::EncodedFormat::Qoi:  formatName = "QOI";  break;
            case presspng::EncodedFormat::Ppm:  formatName = "PPM";  break;
        }
        if (inputs.size() == 1) {
            std::cout << "input:   " << result.inputPath.string() << " ("
                      << presspng::formatBytes(result.inputBytes) << ")\n"
                      << "output:  " << result.outputPath.string() << " ("
                      << presspng::formatBytes(result.outputBytes) << ")\n"
                      << "change:  " << std::fixed << std::setprecision(1)
                      << result.reductionPercent << "%   "
                      << (result.targetMet ? "target MET" : "target NOT met")
                      << "\n"
                      << "format:  " << formatName;
            if (result.encodedFormat == presspng::EncodedFormat::Jpeg) {
                std::cout << " quality " << result.quality;
            }
            std::cout << ", scale " << static_cast<int>(result.scale * 100.0)
                      << "%\n"
                      << "time:    " << result.elapsedMs << " ms\n"
                      << "message: " << result.message << "\n";
        } else {
            std::cout << result.inputPath.filename().string() << " -> "
                      << result.outputPath.filename().string() << " ("
                      << presspng::formatBytes(result.outputBytes) << ", "
                      << std::fixed << std::setprecision(1)
                      << result.reductionPercent << "%, " << formatName;
            if (result.encodedFormat == presspng::EncodedFormat::Jpeg) {
                std::cout << " q" << result.quality;
            }
            std::cout << ", " << static_cast<int>(result.scale * 100.0)
                      << "%)\n";
        }
        if (!result.targetMet) anyNotMet = true;
    }
    if (inputs.size() == 1) return anyNotMet ? 2 : 0;
    return anyNotMet ? 2 : 0;
}
