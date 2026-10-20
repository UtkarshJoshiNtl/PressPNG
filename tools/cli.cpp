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
        "  -f, --format <fmt>      keep | jpeg | png | qoi   (default: keep)\n"
        "  -o, --output <dir>      output directory (default: next to input)\n"
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

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> inputs;
    std::string formatText = "keep";
    std::string outputDir;
    std::uint64_t targetBytes = 0;
    bool quiet = false;

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
    } else if (formatText != "keep") {
        std::cerr << "error: unknown format " << formatText << "\n";
        return 1;
    }

    presspng::CompressionRequest request;
    request.input = inputs.empty() ? "" : inputs[0];
    request.outputDirectory = outputDir;
    request.targetBytes = targetBytes;
    request.format = format;

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
