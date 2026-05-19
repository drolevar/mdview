#include "native/detect_string.hpp"

#include <cstddef>
#include <cstring>

namespace mdview {

namespace {
constexpr const char* kDetectString =
    "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\""
    " | EXT=\"HTML\" | EXT=\"HTM\" | EXT=\"XHTML\"";
}

const char* detect_string() noexcept {
    return kDetectString;
}

void write_detect_string(char* buffer, int maxlen) noexcept {
    if (buffer == nullptr || maxlen <= 0) {
        return;
    }
    const std::size_t cap = static_cast<std::size_t>(maxlen);
    const std::size_t src_len = std::strlen(kDetectString);
    const std::size_t to_copy = (src_len + 1 <= cap) ? src_len : (cap - 1);
    std::memcpy(buffer, kDetectString, to_copy);
    buffer[to_copy] = '\0';
}

}
