#ifndef BFPLAYER_TEXT_ENCODING_H
#define BFPLAYER_TEXT_ENCODING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True when every byte is well-formed UTF-8. ASCII and an empty buffer pass. */
int bfplayer_text_is_utf8(const char* data, size_t size);

/*
 * Decode GB18030, Big5, or UTF-16 (with a BOM) into UTF-8.
 * Returns 0 when the bytes are already UTF-8, empty, or not one of those
 * encodings. Otherwise writes the UTF-8 bytes and returns their length.
 * out_cap must be at least size * 3.
 */
size_t bfplayer_text_decode_utf8(
    const char* data,
    size_t size,
    char* out,
    size_t out_cap);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <string>
#include <string_view>

namespace bfplayer {

inline std::string utf8_text(std::string_view text) {
    if (text.empty() ||
        bfplayer_text_is_utf8(text.data(), text.size())) {
        return std::string(text);
    }
    std::string decoded(text.size() * 3U, '\0');
    const size_t length = bfplayer_text_decode_utf8(
        text.data(),
        text.size(),
        decoded.data(),
        decoded.size());
    if (length == 0) {
        return std::string(text);
    }
    decoded.resize(length);
    return decoded;
}

} // namespace bfplayer

#endif

#endif
