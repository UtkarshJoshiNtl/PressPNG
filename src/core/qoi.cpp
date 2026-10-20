#include "presspng/qoi.h"

#include <array>
#include <cstring>

#include "presspng/errors.h"

namespace presspng {
namespace qoi {

namespace {

// QOI format constants.
constexpr uint32_t kMagic = 0x716F6966; // 'q','o','i','f'
constexpr uint8_t kEndMarker[8] = {0, 0, 0, 0, 0, 0, 0, 1};

constexpr uint8_t kOpRgb = 0b11111110;
constexpr uint8_t kOpRgba = 0b11111111;
constexpr uint8_t kOpIndex = 0b00000000;
constexpr uint8_t kOpDiff = 0b01000000;
constexpr uint8_t kOpLuma = 0b10000000;
constexpr uint8_t kOpRun = 0b11000000;

uint32_t readU32(const std::vector<std::uint8_t>& in, std::size_t& i) {
    uint32_t v = (static_cast<uint32_t>(in[i]) << 24) |
                 (static_cast<uint32_t>(in[i + 1]) << 16) |
                 (static_cast<uint32_t>(in[i + 2]) << 8) |
                 (static_cast<uint32_t>(in[i + 3]));
    i += 4;
    return v;
}

} // namespace

bool decode(const std::vector<std::uint8_t>& in, Image& out) {
    if (in.size() < 14 + 8) {
        return false;
    }
    std::size_t i = 0;
    const uint32_t magic = readU32(in, i);
    if (magic != kMagic) {
        return false;
    }
    const uint32_t width = readU32(in, i);
    const uint32_t height = readU32(in, i);
    const uint8_t channels = in[i++];
    const uint8_t colorspace = in[i++];
    (void)colorspace;

    if (width == 0 || height == 0 || (channels != 3 && channels != 4)) {
        return false;
    }

    // Accept reasonable sizes without importing Image's internal caps.
    out = Image(static_cast<int>(width), static_cast<int>(height));
    std::array<std::array<uint8_t, 4>, 64> index{};
    uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    std::size_t px = 0;
    while (px < pixels && i < in.size()) {
        if (i + 7 < in.size() && std::memcmp(&in[i], kEndMarker, 8) == 0) {
            break;
        }
        const uint8_t b1 = in[i++];
        if (b1 == kOpRgb) {
            pr = in[i++];
            pg = in[i++];
            pb = in[i++];
        } else if (b1 == kOpRgba) {
            pr = in[i++];
            pg = in[i++];
            pb = in[i++];
            pa = in[i++];
        } else if ((b1 & 0b11000000) == kOpIndex) {
            const int idx = b1 & 0b00111111;
            pr = index[idx][0];
            pg = index[idx][1];
            pb = index[idx][2];
            pa = index[idx][3];
        } else if ((b1 & 0b11000000) == kOpDiff) {
            const int dr = ((b1 >> 4) & 0b11) - 2;
            const int dg = ((b1 >> 2) & 0b11) - 2;
            const int db = (b1 & 0b11) - 2;
            pr = static_cast<uint8_t>(pr + dr);
            pg = static_cast<uint8_t>(pg + dg);
            pb = static_cast<uint8_t>(pb + db);
        } else if ((b1 & 0b11000000) == kOpLuma) {
            if (i >= in.size()) {
                break;
            }
            const uint8_t b2 = in[i++];
            const int dg = (b1 & 0b00111111) - 32;
            int drdg = ((b2 >> 4) & 0b1111) - 8;
            int dbdg = (b2 & 0b1111) - 8;
            pr = static_cast<uint8_t>(pr + drdg + dg);
            pg = static_cast<uint8_t>(pg + dg);
            pb = static_cast<uint8_t>(pb + dbdg + dg);
        } else if ((b1 & 0b11000000) == kOpRun) {
            const int run = (b1 & 0b00111111) + 1;
            for (int r = 0; r < run && px < pixels; ++r) {
                out.setPixel(static_cast<int>(px % width),
                             static_cast<int>(px / width), pr, pg, pb, pa);
                ++px;
            }
            const int idx2 =
                ((pr * 3 + pg * 5 + pb * 7 + pa * 11) & 0x3f);
            index[idx2] = {pr, pg, pb, pa};
            continue;
        } else {
            return false;
        }

        out.setPixel(static_cast<int>(px % width),
                     static_cast<int>(px / width), pr, pg, pb, pa);
        ++px;
        const int idx2 =
            ((pr * 3 + pg * 5 + pb * 7 + pa * 11) & 0x3f);
        index[idx2] = {pr, pg, pb, pa};
    }
    return px == pixels;
}

std::vector<std::uint8_t> encode(const Image& image) {
    if (image.empty()) {
        throw CompressionError(ErrorCode::EncodeFailed, "cannot encode an empty image");
    }
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(image.width()) * image.height() * 4 +
                14 + 8);
    auto pushU32 = [&](uint32_t v) {
        out.push_back((v >> 24) & 0xff);
        out.push_back((v >> 16) & 0xff);
        out.push_back((v >> 8) & 0xff);
        out.push_back(v & 0xff);
    };
    pushU32(kMagic);
    pushU32(static_cast<uint32_t>(image.width()));
    pushU32(static_cast<uint32_t>(image.height()));
    out.push_back(4); // RGBA
    out.push_back(0); // sRGB with linear alpha

    std::array<std::array<uint8_t, 4>, 64> index{};
    uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
    int run = 0;
    const int total = image.width() * image.height();

    auto flushRun = [&]() {
        if (run == 0) {
            return;
        }
        while (run > 0) {
            const int chunk = std::min(run, 62);
            out.push_back(kOpRun | (chunk - 1));
            run -= chunk;
        }
    };

    for (int i = 0; i < total; ++i) {
        const int x = i % image.width();
        const int y = i / image.width();
        const auto px = image.pixel(x, y);
        uint8_t r = px[0], g = px[1], b = px[2], a = px[3];

        if (r == pr && g == pg && b == pb && a == pa && run < 62) {
            ++run;
            continue;
        }
        flushRun();

        const int idx = ((r * 3 + g * 5 + b * 7 + a * 11) & 0x3f);
        if (index[idx][0] == r && index[idx][1] == g && index[idx][2] == b &&
            index[idx][3] == a) {
            out.push_back(kOpIndex | idx);
        } else if (a == pa) {
            const int dr = static_cast<int>(r) - pr;
            const int dg = static_cast<int>(g) - pg;
            const int db = static_cast<int>(b) - pb;
            const int drdg = dr - dg;
            const int dbdg = db - dg;
            if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 &&
                db <= 1) {
                out.push_back(kOpDiff | ((dr + 2) << 4) | ((dg + 2) << 2) |
                              (db + 2));
            } else if (drdg >= -8 && drdg <= 7 && dg >= -32 && dg <= 31 &&
                       dbdg >= -8 && dbdg <= 7) {
                out.push_back(kOpLuma | (dg + 32));
                out.push_back(((drdg + 8) << 4) | (dbdg + 8));
            } else {
                out.push_back(kOpRgb);
                out.push_back(r);
                out.push_back(g);
                out.push_back(b);
            }
        } else {
            out.push_back(kOpRgba);
            out.push_back(r);
            out.push_back(g);
            out.push_back(b);
            out.push_back(a);
        }

        index[idx] = {r, g, b, a};
        pr = r; pg = g; pb = b; pa = a;
    }
    flushRun();
    out.insert(out.end(), kEndMarker, kEndMarker + 8);
    return out;
}

} // namespace qoi
} // namespace presspng
