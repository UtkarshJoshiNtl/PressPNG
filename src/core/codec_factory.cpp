#include "presspng/codec_factory.h"

#include <memory>
#include <utility>

#include "presspng/codecs.h"

namespace presspng {

CodecFactory::Selection CodecFactory::create(UserFormat choice,
                                             InputFormat source) {
    Selection selection;
    bool fellBack = false;
    const char* note = nullptr;
    EncodedFormat resolved = EncodedFormat::Png;

    if (choice == UserFormat::Jpeg) {
        resolved = EncodedFormat::Jpeg;
    } else if (choice == UserFormat::Png) {
        resolved = EncodedFormat::Png;
    } else if (choice == UserFormat::Qoi) {
        resolved = EncodedFormat::Qoi;
    } else {
        switch (source) {
            case InputFormat::Jpeg:
                resolved = EncodedFormat::Jpeg;
                break;
            case InputFormat::Png:
                resolved = EncodedFormat::Png;
                break;
            case InputFormat::Bmp:
                resolved = EncodedFormat::Bmp;
                break;
            case InputFormat::Tga:
                resolved = EncodedFormat::Tga;
                break;
            case InputFormat::Qoi:
                resolved = EncodedFormat::Qoi;
                break;
            default:
                resolved = EncodedFormat::Png;
                fellBack = true;
                note = "source format has no writer, saving as PNG";
                break;
        }
    }

    switch (resolved) {
        case EncodedFormat::Jpeg:
            selection.codec = std::make_unique<JpegCodec>();
            break;
        case EncodedFormat::Png:
            selection.codec = std::make_unique<PngCodec>();
            break;
        case EncodedFormat::Bmp:
            selection.codec = std::make_unique<BmpCodec>();
            break;
        case EncodedFormat::Tga:
            selection.codec = std::make_unique<TgaCodec>();
            break;
        case EncodedFormat::Qoi:
            selection.codec = std::make_unique<QoiCodec>();
            break;
    }

    selection.resolved = resolved;
    selection.fellBack = fellBack;
    selection.fallbackNote = note;
    return selection;
}

} // namespace presspng
