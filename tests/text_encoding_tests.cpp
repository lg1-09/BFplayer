#include "bfplayer/text_encoding.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    const std::string utf8 = "中文";
    check(
        bfplayer_text_is_utf8(utf8.data(), utf8.size()) == 1,
        "UTF-8 Chinese stays UTF-8");
    check(
        bfplayer::utf8_text(utf8) == utf8,
        "valid UTF-8 is not recoded");

    const unsigned char gbk[] = {0xD6, 0xD0, 0xCE, 0xC4};
    check(
        bfplayer_text_is_utf8(
            reinterpret_cast<const char*>(gbk),
            sizeof(gbk)) == 0,
        "GBK Chinese is not valid UTF-8");
    check(
        bfplayer::utf8_text(std::string(
            reinterpret_cast<const char*>(gbk),
            sizeof(gbk))) == utf8,
        "GBK Chinese decodes to UTF-8");

    const unsigned char big5[] = {0xBA, 0x7E, 0xA6, 0x72};
    check(
        bfplayer::utf8_text(std::string(
            reinterpret_cast<const char*>(big5),
            sizeof(big5))) == "漢字",
        "Big5 Chinese decodes when the bytes are not GBK");

    const unsigned char gb18030[] = {0x95, 0x32, 0x82, 0x36};
    const std::string extension = bfplayer::utf8_text(std::string(
        reinterpret_cast<const char*>(gb18030),
        sizeof(gb18030)));
    check(extension.size() == 4, "GB18030 extension B uses four UTF-8 bytes");
    check(
        static_cast<unsigned char>(extension[0]) == 0xF0 &&
            static_cast<unsigned char>(extension[1]) == 0xA0 &&
            static_cast<unsigned char>(extension[2]) == 0x80 &&
            static_cast<unsigned char>(extension[3]) == 0x80,
        "GB18030 four-byte sequence decodes to U+20000");

    const unsigned char utf16[] = {0xFF, 0xFE, 0x2D, 0x4E};
    check(
        bfplayer::utf8_text(std::string(
            reinterpret_cast<const char*>(utf16),
            sizeof(utf16))) == "中",
        "UTF-16 LE BOM decodes Chinese");

    const unsigned char unknown[] = {0xFF};
    check(
        bfplayer::utf8_text(std::string(
            reinterpret_cast<const char*>(unknown),
            sizeof(unknown))) ==
            std::string(reinterpret_cast<const char*>(unknown), sizeof(unknown)),
        "unrecognized bytes are left unchanged");

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "text_encoding_tests: PASS\n";
    return 0;
}
