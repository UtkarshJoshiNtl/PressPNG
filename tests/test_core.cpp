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

static void testPpmRoundTrip() {
    const Image source = makeTestImage(48, 32);
    const PpmCodec codec;
    CHECK(codec.hasQualityKnob() == false);

    const auto encoded = codec.encode(source, EncodeOptions{});
    CHECK(!encoded.empty());
    // Binary PPM magic + header.
    CHECK(encoded.size() > 2 && encoded[0] == 'P' && encoded[1] == '6');

    const Image decoded = ImageLoader::decode(encoded);
    CHECK(decoded.width() == source.width());
    CHECK(decoded.height() == source.height());

    // PPM is lossless but has no alpha channel, so compare RGB only.
    bool identical = true;
    for (int y = 0; y < source.height() && identical; ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const auto a = source.pixel(x, y);
            const auto b = decoded.pixel(x, y);
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
                identical = false;
                break;
            }
        }
    }
    CHECK(identical);
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

    auto forcePpm = CodecFactory::create(UserFormat::Ppm, InputFormat::Png);
    CHECK(forcePpm.resolved == EncodedFormat::Ppm);
    CHECK(forcePpm.codec != nullptr);

    auto keepPnm = CodecFactory::create(UserFormat::Keep, InputFormat::Pnm);
    CHECK(keepPnm.resolved == EncodedFormat::Ppm);
    CHECK(!keepPnm.fellBack);
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

static bool sameImage(const Image& a, const Image& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) return false;
        }
    }
    return true;
}

static Image makeMarkerImage(int width, int height) {
    Image image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixel(x, y, static_cast<std::uint8_t>(x * 20 + 1),
                           static_cast<std::uint8_t>(y * 30 + 2),
                           static_cast<std::uint8_t>(x + y), 255);
        }
    }
    return image;
}

static void testImageTransforms() {
    const Image source = makeMarkerImage(4, 3);

    CHECK(source.cropped(1, 1, 2, 2).width() == 2);
    CHECK(source.cropped(1, 1, 2, 2).height() == 2);
    CHECK(source.cropped(1, 1, 2, 2).pixel(0, 0) == source.pixel(1, 1));
    // Crop clamps rather than throwing when it runs off the edge.
    CHECK(source.cropped(3, 2, 99, 99).width() == 1);
    CHECK(source.cropped(3, 2, 99, 99).height() == 1);

    const Image cw90 = source.rotated(Rotate::Cw90);
    CHECK(cw90.width() == 3 && cw90.height() == 4);
    CHECK(cw90.pixel(2, 0) == source.pixel(0, 0)); // top-left -> top-right
    CHECK(sameImage(cw90.rotated(Rotate::Cw270), source));

    const Image cw180 = source.rotated(Rotate::Cw180);
    CHECK(cw180.pixel(3, 2) == source.pixel(0, 0));
    CHECK(sameImage(cw180.rotated(Rotate::Cw180), source));
    CHECK(sameImage(source.rotated(Rotate::None), source));

    const Image flipH = source.flipped(true);
    CHECK(flipH.pixel(0, 0) == source.pixel(3, 0));
    CHECK(sameImage(flipH.flipped(true), source));

    const Image flipV = source.flipped(false);
    CHECK(flipV.pixel(0, 0) == source.pixel(0, 2));
    CHECK(sameImage(flipV.flipped(false), source));
}

static void testTransformSpec() {
    const Image source = makeMarkerImage(800, 400);

    CHECK(TransformSpec{}.isIdentity());
    TransformSpec notIdentity;
    notIdentity.rotate = Rotate::Cw90;
    CHECK(!notIdentity.isIdentity());

    // Identity leaves the pixels untouched.
    CHECK(sameImage(applyTransform(source, TransformSpec{}), source));

    // Requested size is a ceiling; it is honoured when smaller.
    TransformSpec shrink;
    shrink.outputWidth = 400;
    shrink.outputHeight = 200;
    const Image small = applyTransform(source, shrink);
    CHECK(small.width() == 400 && small.height() == 200);

    // ...and ignored (never upscaled) when larger than the source.
    TransformSpec grow;
    grow.outputWidth = 1600;
    grow.outputHeight = 800;
    const Image big = applyTransform(source, grow);
    CHECK(big.width() == 800 && big.height() == 400);

    // A single axis keeps the aspect ratio.
    TransformSpec halfWidth;
    halfWidth.outputWidth = 200;
    const Image derived = applyTransform(source, halfWidth);
    CHECK(derived.width() == 200 && derived.height() == 100);

    // Normalized crop is relative to the source.
    TransformSpec crop;
    crop.cropX = 0.25;
    crop.cropY = 0.5;
    crop.cropW = 0.5;
    crop.cropH = 0.5;
    const Image cropped = applyTransform(source, crop);
    CHECK(cropped.width() == 400 && cropped.height() == 200);
    CHECK(cropped.pixel(0, 0) == source.pixel(200, 200));
}

static void testEstimateMatchesCompress() {
    const fs::path dir = testDir();
    const fs::path input = dir / "estimate_input.png";
    writeBytes(input, PngCodec().encode(makeTestImage(640, 480), EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 30000;
    request.format = UserFormat::Jpeg;

    Compressor compressor;
    const EstimateResult estimate = compressor.estimate(request);
    CHECK(estimate.ok);
    CHECK(!estimate.bytes.empty());
    CHECK(estimate.outputBytes == estimate.bytes.size());
    CHECK(estimate.targetMet);
    CHECK(estimate.outputBytes <= 30000u);

    // The estimate writes nothing...
    CHECK(!fs::exists(dir / "estimate_input_compressed.jpg"));

    // ...and predicts the real run exactly.
    const CompressionResult real = compressor.compress(request);
    CHECK(real.ok);
    CHECK(real.outputBytes == estimate.outputBytes);
    CHECK(real.quality == estimate.quality);

    // The estimated bytes decode back to a real image.
    const Image decoded = ImageLoader::decode(estimate.bytes);
    CHECK(decoded.width() == 640 && decoded.height() == 480);
}

static void testCompressRespectsResolution() {
    const fs::path dir = testDir();
    const fs::path input = dir / "resize_input.png";
    writeBytes(input, PngCodec().encode(makeTestImage(800, 600), EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 400000; // generous, so quality alone fits
    request.format = UserFormat::Jpeg;
    request.transform.outputWidth = 400;
    request.transform.outputHeight = 300;

    Compressor compressor;
    const CompressionResult result = compressor.compress(request);
    CHECK(result.ok);
    CHECK(result.scale < 0.6 && result.scale > 0.4); // ~0.5, no extra downscale

    const Image decoded = ImageLoader::decode(ImageLoader::readFile(result.outputPath));
    CHECK(decoded.width() == 400 && decoded.height() == 300);

    // Requesting an upscale is a no-op, not a blur.
    CompressionRequest bigger;
    bigger.input = input;
    bigger.targetBytes = 400000;
    bigger.format = UserFormat::Jpeg;
    bigger.transform.outputWidth = 1600;
    bigger.transform.outputHeight = 1200;
    const CompressionResult same = compressor.compress(bigger);
    CHECK(same.ok);
    const Image kept = ImageLoader::decode(ImageLoader::readFile(same.outputPath));
    CHECK(kept.width() == 800 && kept.height() == 600);
}

static void testKeepFastPathSkippedWhenTransforming() {
    const fs::path dir = testDir();
    const fs::path input = dir / "small_transform.png";
    writeBytes(input, PngCodec().encode(makeTestImage(64, 64), EncodeOptions{}));

    CompressionRequest request;
    request.input = input;
    request.targetBytes = 1000000; // source already far below
    request.format = UserFormat::Keep;
    request.transform.outputWidth = 32;
    request.transform.outputHeight = 32;

    Compressor compressor;
    const CompressionResult result = compressor.compress(request);
    CHECK(result.ok);
    CHECK(result.outputBytes != result.inputBytes); // re-encoded, not copied
    const Image decoded = ImageLoader::decode(ImageLoader::readFile(result.outputPath));
    CHECK(decoded.width() == 32 && decoded.height() == 32);
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
    testPpmRoundTrip();
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
    testImageTransforms();
    testTransformSpec();
    testEstimateMatchesCompress();
    testCompressRespectsResolution();
    testKeepFastPathSkippedWhenTransforming();

    std::cout << (g_checks - g_failures) << "/" << g_checks
              << " checks passed\n";
    if (g_failures > 0) {
        std::cerr << g_failures << " FAILURES\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
