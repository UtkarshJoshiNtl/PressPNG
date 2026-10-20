#include "presspng/compressor.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <utility>

#include "presspng/codec_factory.h"
#include "presspng/errors.h"
#include "presspng/format_bytes.h"
#include "presspng/image_loader.h"
#include "presspng/target_size_solver.h"

namespace presspng {

namespace fs = std::filesystem;

namespace {

namespace fs = std::filesystem;

EncodedFormat mappedFormat(InputFormat input) {
    switch (input) {
        case InputFormat::Jpeg:
            return EncodedFormat::Jpeg;
        case InputFormat::Png:
            return EncodedFormat::Png;
        case InputFormat::Bmp:
            return EncodedFormat::Bmp;
        case InputFormat::Tga:
            return EncodedFormat::Tga;
        case InputFormat::Qoi:
            return EncodedFormat::Qoi;
        default:
            return EncodedFormat::Png;
    }
}

fs::path uniqueOutputPath(const fs::path& directory,
                          const fs::path& stemWithSuffix,
                          const std::string& extension) {
    fs::path candidate = directory / stemWithSuffix;
    candidate += extension;
    int counter = 2;
    while (fs::exists(candidate) && counter < 1000) {
        candidate = directory / stemWithSuffix;
        candidate += std::to_string(counter) + extension;
        ++counter;
    }
    return candidate;
}

} // namespace

Compressor::Compressor(std::unique_ptr<ISizeSolver> solver)
    : solver_(solver ? std::move(solver)
                     : std::make_unique<TargetSizeSolver>()) {}

CompressionResult Compressor::compress(const CompressionRequest& request,
                                       ProgressFn progress,
                                       const CancelToken* cancel) const {
    const auto started = std::chrono::steady_clock::now();
    CompressionResult result;
    result.inputPath = request.input;

    auto report = [&](const std::string& text) {
        if (progress) {
            progress(text);
        }
    };

    try {
        if (request.targetBytes == 0) {
            throw CompressionError(ErrorCode::BadRequest,
                                   "target size must be greater than zero");
        }

        report("Reading file...");
        const std::vector<std::uint8_t> raw = ImageLoader::readFile(request.input);
        result.inputBytes = raw.size();
        const InputFormat inputFormat = ImageLoader::sniff(raw);

        const fs::path directory = request.outputDirectory.empty()
                                       ? request.input.parent_path()
                                       : request.outputDirectory;
        fs::path stem = request.input.stem();
        stem += "_compressed";

        const auto fastSelection = CodecFactory::create(request.format, inputFormat);

        // Fast path: already small enough and the user wants the original kept.
        if (request.format == UserFormat::Keep && !raw.empty() &&
            raw.size() <= request.targetBytes && !fastSelection.fellBack) {
            report("Already within target size - copying unchanged.");
            const std::string extension = fastSelection.codec->extension();
            const fs::path output = request.outputPath.empty()
                                        ? uniqueOutputPath(directory, stem, extension)
                                        : request.outputPath;
            ImageLoader::writeFile(output, raw);

            result.ok = true;
            result.targetMet = true;
            result.outputPath = output;
            result.outputBytes = raw.size();
            result.reductionPercent = 0.0;
            result.encodedFormat = mappedFormat(inputFormat);
            result.scale = 1.0;
            result.message = "already within target size - copied unchanged";
            report(result.message);
            result.elapsedMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            return result;
        }

        report("Decoding image...");
        Image image = ImageLoader::decode(raw);

        if (cancel != nullptr && cancel->cancelled()) {
            throw CompressionError(ErrorCode::Cancelled, "cancelled by user");
        }

        CodecFactory::Selection selection =
            CodecFactory::create(request.format, inputFormat);

        SolvedImage solved = solver_->solve(
            image, *selection.codec, request.targetBytes, report, cancel);

        report("Writing output...");
        const fs::path output = request.outputPath.empty()
                                    ? uniqueOutputPath(directory, stem, selection.codec->extension())
                                    : request.outputPath;
        ImageLoader::writeFile(output, solved.bytes);

        result.ok = true;
        result.targetMet = solved.targetMet;
        result.outputPath = output;
        result.outputBytes = solved.bytes.size();
        result.encodedFormat = selection.resolved;
        result.quality = solved.options.quality;
        result.scale = solved.scale;
        result.reductionPercent =
            result.inputBytes > 0
                ? 100.0 * (1.0 - static_cast<double>(result.outputBytes) /
                                     static_cast<double>(result.inputBytes))
                : 0.0;

        result.message = solved.message;
        if (selection.fellBack && selection.fallbackNote) {
            result.message =
                std::string(selection.fallbackNote) + "; " + result.message;
        }
        report(result.message);
    } catch (const CompressionError& error) {
        result.ok = false;
        result.cancelled = error.code() == ErrorCode::Cancelled;
        result.message = error.what();
    } catch (const std::exception& error) {
        result.ok = false;
        result.message = std::string("unexpected error: ") + error.what();
    }

    result.elapsedMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return result;
}

} // namespace presspng
