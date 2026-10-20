#pragma once

#include <memory>

#include "presspng/codec.h"

namespace presspng {

// Creates the right concrete codec for the user's choice + source format.
class CodecFactory {
public:
    struct Selection {
        std::unique_ptr<IImageCodec> codec;
        EncodedFormat resolved = EncodedFormat::Png;
        bool fellBack = false;
        const char* fallbackNote = nullptr;
    };

    static Selection create(UserFormat choice, InputFormat source);
};

} // namespace presspng
