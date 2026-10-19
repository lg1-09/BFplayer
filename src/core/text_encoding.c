#include "bfplayer/text_encoding.h"

#include <stddef.h>
#include <stdint.h>

#include "gbk_bmp.inc"
#include "big5_bmp.inc"
#include "gb18030_ranges.inc"

static int append_codepoint(
    char* out,
    size_t out_cap,
    size_t* used,
    uint32_t codepoint) {
    unsigned char bytes[4];
    int count = 0;
    if (codepoint <= 0x7FU) {
        bytes[0] = (unsigned char)codepoint;
        count = 1;
    } else if (codepoint <= 0x7FFU) {
        bytes[0] = (unsigned char)(0xC0U | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        count = 2;
    } else if (codepoint <= 0xFFFFU) {
        if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) {
            return 0;
        }
        bytes[0] = (unsigned char)(0xE0U | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        count = 3;
    } else if (codepoint <= 0x10FFFFU) {
        bytes[0] = (unsigned char)(0xF0U | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80U | ((codepoint >> 12) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[3] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        count = 4;
    } else {
        return 0;
    }
    if (*used + (size_t)count > out_cap) {
        return 0;
    }
    for (int index = 0; index < count; ++index) {
        out[*used + (size_t)index] = (char)bytes[index];
    }
    *used += (size_t)count;
    return 1;
}

int bfplayer_text_is_utf8(const char* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    size_t index = 0;
    if (size > 0 && !data) {
        return 0;
    }
    while (index < size) {
        const unsigned char lead = bytes[index];
        size_t count = 1;
        if (lead <= 0x7FU) {
            index += 1;
            continue;
        }
        if (lead < 0xC2U || lead > 0xF4U) {
            return 0;
        }
        if (lead < 0xE0U) {
            count = 2;
        } else if (lead < 0xF0U) {
            count = 3;
        } else {
            count = 4;
        }
        if (index + count > size) {
            return 0;
        }
        for (size_t extra = 1; extra < count; ++extra) {
            if ((bytes[index + extra] & 0xC0U) != 0x80U) {
                return 0;
            }
        }
        if (lead == 0xE0U && bytes[index + 1] < 0xA0U) {
            return 0;
        }
        if (lead == 0xEDU && bytes[index + 1] > 0x9FU) {
            return 0;
        }
        if (lead == 0xF0U && bytes[index + 1] < 0x90U) {
            return 0;
        }
        if (lead == 0xF4U && bytes[index + 1] > 0x8FU) {
            return 0;
        }
        index += count;
    }
    return 1;
}

static int gbk_codepoint(unsigned int lead, unsigned int trail) {
    unsigned int column;
    unsigned int index;
    if (lead < 0x81U || lead > 0xFEU || trail < 0x40U || trail == 0x7FU ||
        trail > 0xFEU) {
        return -1;
    }
    column = trail - 0x40U;
    if (trail > 0x7FU) {
        column -= 1U;
    }
    index = (lead - 0x81U) * 190U + column;
    if (index >= (unsigned int)(sizeof(kGbkBmp) / sizeof(kGbkBmp[0]))) {
        return -1;
    }
    return kGbkBmp[index] == 0 ? -1 : (int)kGbkBmp[index];
}

static int big5_codepoint(unsigned int lead, unsigned int trail) {
    unsigned int column;
    unsigned int index;
    if (lead < 0x81U || lead > 0xFEU) {
        return -1;
    }
    if (trail >= 0x40U && trail <= 0x7EU) {
        column = trail - 0x40U;
    } else if (trail >= 0xA1U && trail <= 0xFEU) {
        column = 63U + (trail - 0xA1U);
    } else {
        return -1;
    }
    index = (lead - 0x81U) * 157U + column;
    if (index >= (unsigned int)(sizeof(kBig5Bmp) / sizeof(kBig5Bmp[0]))) {
        return -1;
    }
    return kBig5Bmp[index] == 0 ? -1 : (int)kBig5Bmp[index];
}

static uint32_t gb18030_four_byte(
    unsigned int b1,
    unsigned int b2,
    unsigned int b3,
    unsigned int b4) {
    const unsigned int count =
        (unsigned int)(sizeof(kGb18030Ranges) / sizeof(kGb18030Ranges[0]));
    unsigned int linear;
    unsigned int low = 0;
    unsigned int high = count;
    if (b1 < 0x81U || b1 > 0xFEU || b2 < 0x30U || b2 > 0x39U || b3 < 0x81U ||
        b3 > 0xFEU || b4 < 0x30U || b4 > 0x39U) {
        return 0;
    }
    linear = ((b1 - 0x81U) * 10U + (b2 - 0x30U)) * 1260U +
        (b3 - 0x81U) * 10U + (b4 - 0x30U);
    while (low < high) {
        const unsigned int mid = low + (high - low) / 2U;
        if (kGb18030Ranges[mid][1] < linear) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    if (low >= count || linear < kGb18030Ranges[low][0] ||
        linear > kGb18030Ranges[low][1]) {
        return 0;
    }
    return kGb18030Ranges[low][2] + (linear - kGb18030Ranges[low][0]);
}

static int decode_gb18030(
    const unsigned char* bytes,
    size_t size,
    char* out,
    size_t out_cap,
    size_t* used) {
    size_t index = 0;
    *used = 0;
    while (index < size) {
        const unsigned int lead = bytes[index];
        if (lead < 0x80U) {
            if (!append_codepoint(out, out_cap, used, lead)) {
                return 0;
            }
            index += 1;
            continue;
        }
        if (index + 4U <= size) {
            const uint32_t four = gb18030_four_byte(
                bytes[index],
                bytes[index + 1],
                bytes[index + 2],
                bytes[index + 3]);
            if (four != 0) {
                if (!append_codepoint(out, out_cap, used, four)) {
                    return 0;
                }
                index += 4;
                continue;
            }
        }
        if (index + 1U >= size) {
            return 0;
        }
        const int codepoint = gbk_codepoint(lead, bytes[index + 1]);
        if (codepoint < 0 ||
            !append_codepoint(out, out_cap, used, (uint32_t)codepoint)) {
            return 0;
        }
        index += 2;
    }
    return 1;
}

static int decode_big5(
    const unsigned char* bytes,
    size_t size,
    char* out,
    size_t out_cap,
    size_t* used) {
    size_t index = 0;
    *used = 0;
    while (index < size) {
        const unsigned int lead = bytes[index];
        if (lead < 0x80U) {
            if (!append_codepoint(out, out_cap, used, lead)) {
                return 0;
            }
            index += 1;
            continue;
        }
        if (index + 1U >= size) {
            return 0;
        }
        const int codepoint = big5_codepoint(lead, bytes[index + 1]);
        if (codepoint < 0 ||
            !append_codepoint(out, out_cap, used, (uint32_t)codepoint)) {
            return 0;
        }
        index += 2;
    }
    return 1;
}

static int decode_utf16(
    const unsigned char* bytes,
    size_t size,
    int big_endian,
    char* out,
    size_t out_cap,
    size_t* used) {
    size_t index = 0;
    *used = 0;
    if ((size % 2U) != 0U) {
        return 0;
    }
    while (index < size) {
        const unsigned int unit = big_endian
            ? ((unsigned int)bytes[index] << 8) | bytes[index + 1]
            : (unsigned int)bytes[index] | ((unsigned int)bytes[index + 1] << 8);
        uint32_t codepoint = unit;
        index += 2;
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            unsigned int low;
            if (index + 1U >= size) {
                return 0;
            }
            low = big_endian
                ? ((unsigned int)bytes[index] << 8) | bytes[index + 1]
                : (unsigned int)bytes[index] |
                    ((unsigned int)bytes[index + 1] << 8);
            if (low < 0xDC00U || low > 0xDFFFU) {
                return 0;
            }
            codepoint = 0x10000U + (((unit - 0xD800U) << 10) | (low - 0xDC00U));
            index += 2;
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return 0;
        }
        if (!append_codepoint(out, out_cap, used, codepoint)) {
            return 0;
        }
    }
    return 1;
}

size_t bfplayer_text_decode_utf8(
    const char* data,
    size_t size,
    char* out,
    size_t out_cap) {
    const unsigned char* bytes = (const unsigned char*)data;
    size_t used = 0;
    if (!data || !out || size == 0 || bfplayer_text_is_utf8(data, size)) {
        return 0;
    }
    if (size >= 2 && bytes[0] == 0xFFU && bytes[1] == 0xFEU) {
        return decode_utf16(bytes + 2, size - 2, 0, out, out_cap, &used) ? used
                                                                           : 0;
    }
    if (size >= 2 && bytes[0] == 0xFEU && bytes[1] == 0xFFU) {
        return decode_utf16(bytes + 2, size - 2, 1, out, out_cap, &used) ? used
                                                                           : 0;
    }
    if (decode_gb18030(bytes, size, out, out_cap, &used)) {
        return used;
    }
    if (decode_big5(bytes, size, out, out_cap, &used)) {
        return used;
    }
    return 0;
}
