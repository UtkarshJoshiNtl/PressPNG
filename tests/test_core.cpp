// PressPNG core test suite - plain asserts, no external framework.
// Every test prints PASS/FAIL; the process exits non-zero on any failure.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "presspng/codec_factory.h"
#include "presspng/codecs.h"
#include "presspng/compression_request.h"
#include "presspng/compressor.h"
#include "presspng/errors.h"
#include "presspng/image.h"
#include "presspng/image_loader.h"
#include "presspng/preview.h"
#include "presspng/target_size_solver.h"

namespace fs = std::filesystem;
using namespace presspng;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  "    \
                      << #cond << "\n";                                    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static fs::path testDir() {
    return fs::temp_directory_path() / "presspng_tests";
}

// Deterministic pseudo-random image: smooth gradient + noise, so JPEG has
// realistic (non-trivial) content to compress.
static Image makeTestImage(int width, int height) {
    Image image(width, height);
    std::uint32_t seed = 0x12345678u;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const int noise = static_cast<int>((seed >> 24) % 40u);
            const int r = (x * 255) / (width > 1 ? width - 1 : 1);
            const int g = (y * 255) / (height > 1 ? height - 1 : 1);
            const int b = ((x + y) * 255) / ((width + height) > 1
                                                     ? width + height - 1
                                                     : 1);
            image.setPixel(x, y,
                           static_cast<std::uint8_t>((r + noise) % 256),
                           static_cast<std::uint8_t>((g + noise) % 256),
                           static_cast<std::uint8_t>((b + noise) % 256), 255);
        }
    }
    return image;
}

static void writeBytes(const fs::path& path,
                       const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// ---------------------------------------------------------------- tests

static void testImageBasics() {
    Image image(4, 3);
    CHECK(!image.empty());
    CHECK(image.width() == 4);
    CHECK(image.height() == 3);
    CHECK(image.byteSize() == 4u * 3u * 4u);

    image.setPixel(1, 1, 10, 20, 30, 40);
    const auto px = image.pixel(1, 1);
    CHECK(px[0] == 10 && px[1] == 20 && px[2] == 30 && px[3] == 40);

    bool threw = false;
    try {
        Image bad(0, 5);
    } catch (const CompressionError&) {
        threw = true;
    }
    CHECK(threw);
}

static void testResizeAverages() {
    Image image(2, 2);
    image.setPixel(0, 0, 0, 0, 0, 255);
    image.setPixel(1, 0, 10, 10, 10, 255);
    image.setPixel(0, 1, 20, 20, 20, 255);
    image.setPixel(1, 1, 30, 30, 30, 255);

    const Image small = image.resized(1, 1);
    CHECK(small.width() == 1 && small.height() == 1);
    const auto px = small.pixel(0, 0);
    CHECK(px[0] == 15); // (0 + 10 + 20 + 30) / 4
}

static void testSniff() {
    const std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G',
                                           0x0D, 0x0A, 0x1A, 0x0A, 0x00};
    CHECK(ImageLoader::sniff(png) == InputFormat::Png);

    const std::vector<std::uint8_t> jpg = {0xFF, 0xD8, 0xFF, 0xE0};
    CHECK(ImageLoader::sniff(jpg) == InputFormat::Jpeg);

    const std::vector<std::uint8_t> bmp = {'B', 'M', 0x00, 0x00};
    CHECK(ImageLoader::sniff(bmp) == InputFormat::Bmp);

    const std::vector<std::uint8_t> gif = {'G', 'I', 'F', '8', '9', 'a'};
    CHECK(ImageLoader::sniff(gif) == InputFormat::Gif);

    const std::vector<std::uint8_t> junk = {0x01, 0x02, 0x03, 0x04};
    CHECK(ImageLoader::sniff(junk) == InputFormat::Unknown);
}

static void testPngRoundTrip() {
    const Image source = makeTestImage(64, 48);
    const PngCodec codec;
    const auto encoded = codec.encode(source, EncodeOptions{});
    CHECK(!encoded.empty());

    const Image decoded = ImageLoader::decode(encoded);
    CHECK(decoded.width() == source.width());
    CHECK(decoded.height() == source.height());
    bool identical = true;
    for (int y = 0; y < source.height() && identical; ++y) {
        for (int x = 0; x < source.width(); ++x) {
            if (decoded.pixel(x, y) != source.pixel(x, y)) {
                identical = false;
                break;
            }
        }
    }
    CHECK(identical); // PNG is lossless
}

static void testJpegRoundTrip() {
    const Image source = makeTestImage(96, 64);
    const JpegCodec codec;
    EncodeOptions options;
    options.quality = 95;
    const auto encoded = codec.encode(source, options);
    CHECK(!encoded.empty());

    const Image decoded = ImageLoader::decode(encoded);
    CHECK(decoded.width() == source.width());
    CHECK(decoded.height() == source.height());

    // Mean absolute error should be small at quality 95.
    std::uint64_t total = 0;
    std::uint64_t count = 0;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const auto a = source.pixel(x, y);
            const auto b = decoded.pixel(x, y);
            for (int c = 0; c < 3; ++c) {
                total += a[c] > b[c] ? a[c] - b[c] : b[c] - a[c];
                ++count;
            }
        }
    }
    const double meanError = static_cast<double>(total) / count;
    CHECK(meanError < 12.0);
}

static void testCodecFactory() {
    auto keepGif = CodecFactory::create(UserFormat::Keep, InputFormat::Gif);
    CHECK(keepGif.resolved == EncodedFormat::Png);
    CHECK(keepGif.fellBack);
    CHECK(keepGif.codec != nullptr);

    auto keepJpeg = CodecFactory::create(UserFormat::Keep, InputFormat::Jpeg);
    CHECK(keepJpeg.resolved == EncodedFormat::Jpeg);
    CHECK(!keepJpeg.fellBack);

    auto forceJpeg = CodecFactory::create(UserFormat::Jpeg, InputFormat::Png);
    CHECK(forceJpeg.resolved == EncodedFormat::Jpeg);

    auto keepBmp = CodecFactory::create(UserFormat::Keep, InputFormat::Bmp);
    CHECK(keepBmp.resolved == EncodedFormat::Bmp);
}

static void testSolverDirectJpeg() {
    const Image source = makeTestImage(640, 480);
    const JpegCodec codec;
    TargetSizeSolver solver;
    const std::uint64_t target = 60000; // 60 KB

    const SolvedImage solved =
        solver.solve(source, codec, target, nullptr);
    CHECK(solved.targetMet);
    CHECK(!solved.bytes.empty());
    CHECK(solved.bytes.size() <= target);
    CHECK(solved.options.quality >= 1 && solved.options.quality <= 100);
}

static void testCompressEndToEnd() {
    const fs::path dir = testDir();
    const fs::path input = dir / "end_to_end_input.png";
    writeBytes(input, PngCodec().encode(makeTestImage(800, 600),
                                        EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 50000; // 50 KB
    request.format = UserFormat::Jpeg;

    Compressor compressor;
    const CompressionResult result = compressor.compress(request);
    CHECK(result.ok);
    CHECK(result.targetMet);
    CHECK(result.inputBytes > 50000u);
    CHECK(result.outputBytes <= 50000u);
    CHECK(fs::exists(result.outputPath));
    CHECK(result.outputPath.filename().u8string().find("_compressed") !=
          std::string::npos);
    CHECK(result.reductionPercent > 0.0);

    // Second run must not overwrite the first output.
    const CompressionResult second = compressor.compress(request);
    CHECK(second.ok);
    CHECK(second.outputPath != result.outputPath);
}

static void testCompressKeepFastPath() {
    const fs::path dir = testDir();
    const fs::path input = dir / "small_input.png";
    writeBytes(input, PngCodec().encode(makeTestImage(32, 32),
                                        EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 100000; // 100 KB, source is far below
    request.format = UserFormat::Keep;

    Compressor compressor;
    const CompressionResult result = compressor.compress(request);
    CHECK(result.ok);
    CHECK(result.targetMet);
    CHECK(result.outputBytes == result.inputBytes); // pure copy
    CHECK(result.message.find("copied") != std::string::npos);
}

static void testCompressUnreachableTarget() {
    const fs::path dir = testDir();
    const fs::path input = dir / "unreachable_input.png";
    writeBytes(input, PngCodec().encode(makeTestImage(640, 480),
                                        EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 10; // impossible
    request.format = UserFormat::Png;

    Compressor compressor;
    const CompressionResult result = compressor.compress(request);
    CHECK(result.ok);           // handled gracefully, file still written
    CHECK(!result.targetMet);   // honestly reported
    CHECK(result.outputBytes > 10u);
    CHECK(!result.message.empty());
    CHECK(fs::exists(result.outputPath));
}

static void testErrorHandling() {
    Compressor compressor;

    // Missing file
    CompressionRequest missing;
    missing.input = testDir() / "does_not_exist.png";
    missing.targetBytes = 1000;
    CompressionResult result = compressor.compress(missing);
    CHECK(!result.ok);
    CHECK(!result.message.empty());

    // Zero target
    CompressionRequest zero;
    zero.input = testDir() / "whatever.png";
    zero.targetBytes = 0;
    result = compressor.compress(zero);
    CHECK(!result.ok);

    // Empty file
    const fs::path empty = testDir() / "empty.png";
    writeBytes(empty, {});
    CompressionRequest emptyRequest;
    emptyRequest.input = empty;
    emptyRequest.targetBytes = 1000;
    result = compressor.compress(emptyRequest);
    CHECK(!result.ok);
    CHECK(result.message.find("empty") != std::string::npos);
}

static void testOutputNamingCollision() {
    const fs::path dir = testDir() / "naming";
    fs::create_directories(dir);
    const fs::path input = dir / "photo.png";
    writeBytes(input, PngCodec().encode(makeTestImage(64, 64),
                                        EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 5000;

    Compressor compressor;
    const auto a = compressor.compress(request);
    const auto b = compressor.compress(request);
    CHECK(a.ok && b.ok);
    CHECK(a.outputPath != b.outputPath);
    CHECK(fs::exists(a.outputPath));
    CHECK(fs::exists(b.outputPath));
}

static void testPreviewAspectFit() {
    const Image source = makeTestImage(800, 400);
    writeBytes(testDir() / "preview_source.png",
               PngCodec().encode(source, EncodeOptions{}));

    PreviewService service;
    const FilePreviewSource file(testDir() / "preview_source.png");
    const PreviewRequest request{200, 200};
    const PreviewFrame frame = service.make(file, request);

    CHECK(frame.ok);
    CHECK(frame.sourceWidth == 800);
    CHECK(frame.sourceHeight == 400);
    CHECK(!frame.thumbnail.empty());
    CHECK(frame.thumbnail.width() == 200);  // width is the limiting side
    CHECK(frame.thumbnail.height() == 100); // aspect ratio preserved
    CHECK(frame.thumbnail.width() <= request.maxWidth);
    CHECK(frame.thumbnail.height() <= request.maxHeight);
}

static void testPreviewNeverUpscales() {
    const Image source = makeTestImage(32, 16);
    writeBytes(testDir() / "preview_small.png",
               PngCodec().encode(source, EncodeOptions{}));

    PreviewService service;
    const FilePreviewSource file(testDir() / "preview_small.png");
    const PreviewFrame frame = service.make(file, {400, 400});

    CHECK(frame.ok);
    CHECK(frame.thumbnail.width() == 32);  // unchanged
    CHECK(frame.thumbnail.height() == 16);
    CHECK(frame.scale == 1.0);
}

static void testPreviewBytesSource() {
    const Image source = makeTestImage(64, 64);
    const auto encoded = PngCodec().encode(source, EncodeOptions{});

    PreviewService service;
    const BytesPreviewSource bytes(encoded);
    const PreviewFrame frame = service.make(bytes, {32, 32});

    CHECK(frame.ok);
    CHECK(frame.thumbnail.width() == 32);
    CHECK(frame.thumbnail.height() == 32);
    CHECK(bytes.describe() == std::string("bytes"));
}

static void testPreviewErrors() {
    PreviewService service;

    // Missing file: handled, not thrown.
    const FilePreviewSource missing(testDir() / "no_such_preview.png");
    PreviewFrame frame = service.make(missing, {100, 100});
    CHECK(!frame.ok);
    CHECK(!frame.message.empty());

    // Not an image at all.
    const fs::path junk = testDir() / "junk_preview.bin";
    writeBytes(junk, {0x01, 0x02, 0x03, 0x04});
    frame = service.make(FilePreviewSource(junk), {100, 100});
    CHECK(!frame.ok);

    // Nonsense box: rejected up front.
    const Image source = makeTestImage(16, 16);
    writeBytes(testDir() / "preview_box.png",
               PngCodec().encode(source, EncodeOptions{}));
    frame = service.make(FilePreviewSource(testDir() / "preview_box.png"),
                         {0, 100});
    CHECK(!frame.ok);
    CHECK(frame.message.find("box") != std::string::npos);
}

static void testCancelToken() {
    const fs::path dir = testDir();
    const fs::path input = dir / "cancel_input.png";
    writeBytes(input, PngCodec().encode(makeTestImage(800, 600),
                                        EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 40000;
    request.format = UserFormat::Jpeg;

    // Already-cancelled token: the work must stop and report cleanly.
    CancelToken token;
    token.cancel();
    CHECK(token.cancelled());

    Compressor compressor;
    const CompressionResult result = compressor.compress(request, nullptr,
                                                         &token);
    CHECK(!result.ok);
    CHECK(result.cancelled);
    CHECK(result.message.find("cancel") != std::string::npos);

    // Reset makes the same token usable again.
    token.reset();
    CHECK(!token.cancelled());
    const CompressionResult second = compressor.compress(request, nullptr,
                                                          &token);
    CHECK(second.ok);
}

int main() {
    std::error_code ec;
    fs::remove_all(testDir(), ec);
    fs::create_directories(testDir());

    testImageBasics();
    testResizeAverages();
    testSniff();
    testPngRoundTrip();
    testJpegRoundTrip();
    testCodecFactory();
    testSolverDirectJpeg();
    testCompressEndToEnd();
    testCompressKeepFastPath();
    testCompressUnreachableTarget();
    testErrorHandling();
    testOutputNamingCollision();
    testPreviewAspectFit();
    testPreviewNeverUpscales();
    testPreviewBytesSource();
    testPreviewErrors();
    testCancelToken();

    std::cout << (g_checks - g_failures) << "/" << g_checks
              << " checks passed\n";
    if (g_failures > 0) {
        std::cerr << g_failures << " FAILURES\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
