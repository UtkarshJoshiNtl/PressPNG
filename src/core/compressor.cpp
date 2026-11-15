#include "presspng/compressor.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "presspng/codec_factory.h"
#include "presspng/errors.h"
#include "presspng/image_loader.h"
#include "presspng/target_size_solver.h"
#include "presspng/transform.h"

namespace presspng {

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

// Everything compress() and estimate() have in common: read, sniff, decode,
// transform, solve. Throws CompressionError; callers catch.
struct PipelineOutcome {
    bool targetMet = false;
    bool copiedUnchanged = false;
    std::uint64_t inputBytes = 0;
    std::vector<std::uint8_t> bytes;
    std::string extension;
    EncodedFormat encodedFormat = EncodedFormat::Png;
    int quality = 0;
    double scale = 1.0;
    std::string message;
};

PipelineOutcome runPipeline(const ISizeSolver& solver,
                            const CompressionRequest& request,
                            const Image* preloaded,
                            const Compressor::ProgressFn& progress,
                            const CancelToken* cancel) {
    if (request.targetBytes == 0) {
        throw CompressionError(ErrorCode::BadRequest,
                               "target size must be greater than zero");
    }

    auto report = [&](const std::string& text) {
        if (progress) {
            progress(text);
        }
    };

    PipelineOutcome out;
    report("Reading file...");
    const std::vector<std::uint8_t> raw = ImageLoader::readFile(request.input);
    out.inputBytes = raw.size();
    const InputFormat inputFormat = ImageLoader::sniff(raw);

    const auto fastSelection = CodecFactory::create(request.format, inputFormat);

    // Fast path: already small enough, original wanted, no geometry change.
    if (request.format == UserFormat::Keep && !raw.empty() &&
        raw.size() <= request.targetBytes && !fastSelection.fellBack &&
        request.transform.isIdentity()) {
        report("Already within target size - copying unchanged.");
        out.targetMet = true;
        out.copiedUnchanged = true;
        out.bytes = raw;
        out.extension = fastSelection.codec->extension();
        out.encodedFormat = mappedFormat(inputFormat);
        out.scale = 1.0;
        out.message = "already within target size - copied unchanged";
        return out;
    }

    report("Decoding image...");
    Image decoded;
    const Image* base = preloaded;
    if (base == nullptr) {
        decoded = ImageLoader::decode(raw);
        base = &decoded;
    }

    if (cancel != nullptr && cancel->cancelled()) {
        throw CompressionError(ErrorCode::Cancelled, "cancelled by user");
    }

    // The solver only reads, so an untransformed image is passed through
    // without a copy; a transform produces a fresh one.
    Image transformed;
    const Image* working = base;
    if (!request.transform.isIdentity()) {
        report("Applying transform...");
        transformed = applyTransform(*base, request.transform);
        working = &transformed;
    }

    CodecFactory::Selection selection =
        CodecFactory::create(request.format, inputFormat);

    SolvedImage solved = solver.solve(
        *working, *selection.codec, request.targetBytes, report, cancel);

    out.targetMet = solved.targetMet;
    out.bytes = std::move(solved.bytes);
    out.extension = selection.codec->extension();
    out.encodedFormat = selection.resolved;
    out.quality = solved.options.quality;
    out.scale = static_cast<double>(working->width()) / base->width() * solved.scale;
    out.message = solved.message;
    if (selection.fellBack && selection.fallbackNote) {
        out.message = std::string(selection.fallbackNote) + "; " + out.message;
    }
    return out;
}

int elapsedMsSince(const std::chrono::steady_clock::time_point& start) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
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
        const PipelineOutcome outcome =
            runPipeline(*solver_, request, nullptr, progress, cancel);

        const fs::path directory = request.outputDirectory.empty()
                                       ? request.input.parent_path()
                                       : request.outputDirectory;
        fs::path stem = request.input.stem();
        stem += "_compressed";

        const fs::path output = request.outputPath.empty()
                                    ? uniqueOutputPath(directory, stem, outcome.extension)
                                    : request.outputPath;
        if (!outcome.copiedUnchanged) {
            report("Writing output...");
        }
        ImageLoader::writeFile(output, outcome.bytes);

        result.ok = true;
        result.targetMet = outcome.targetMet;
        result.outputPath = output;
        result.inputBytes = outcome.inputBytes;
        result.outputBytes = outcome.bytes.size();
        result.encodedFormat = outcome.encodedFormat;
        result.quality = outcome.quality;
        result.scale = outcome.scale;
        result.reductionPercent =
            outcome.inputBytes > 0
                ? 100.0 * (1.0 - static_cast<double>(result.outputBytes) /
                                     static_cast<double>(outcome.inputBytes))
                : 0.0;
        result.message = outcome.message;
        report(result.message);
    } catch (const CompressionError& error) {
        result.ok = false;
        result.cancelled = error.code() == ErrorCode::Cancelled;
        result.message = error.what();
    } catch (const std::exception& error) {
        result.ok = false;
        result.message = std::string("unexpected error: ") + error.what();
    }

    result.elapsedMs = elapsedMsSince(started);
    return result;
}

EstimateResult Compressor::estimate(const CompressionRequest& request,
                                    const CancelToken* cancel) const {
    EstimateResult result;
    try {
        PipelineOutcome outcome = runPipeline(*solver_, request, nullptr, nullptr, cancel);
        result.ok = true;
        result.targetMet = outcome.targetMet;
        result.inputBytes = outcome.inputBytes;
        result.outputBytes = outcome.bytes.size();
        result.encodedFormat = outcome.encodedFormat;
        result.quality = outcome.quality;
        result.scale = outcome.scale;
        result.bytes = std::move(outcome.bytes);
        result.message = outcome.message;
    } catch (const CompressionError& error) {
        result.ok = false;
        result.message = error.what();
    } catch (const std::exception& error) {
        result.ok = false;
        result.message = std::string("unexpected error: ") + error.what();
    }
    return result;
}

EstimateResult Compressor::estimate(const Image& source,
                                    const CompressionRequest& request,
                                    const CancelToken* cancel) const {
    EstimateResult result;
    try {
        PipelineOutcome outcome =
            runPipeline(*solver_, request, &source, nullptr, cancel);
        result.ok = true;
        result.targetMet = outcome.targetMet;
        result.inputBytes = outcome.inputBytes;
        result.outputBytes = outcome.bytes.size();
        result.encodedFormat = outcome.encodedFormat;
        result.quality = outcome.quality;
        result.scale = outcome.scale;
        result.bytes = std::move(outcome.bytes);
        result.message = outcome.message;
    } catch (const CompressionError& error) {
        result.ok = false;
        result.message = error.what();
    } catch (const std::exception& error) {
        result.ok = false;
        result.message = std::string("unexpected error: ") + error.what();
    }
    return result;
}

} // namespace presspng
