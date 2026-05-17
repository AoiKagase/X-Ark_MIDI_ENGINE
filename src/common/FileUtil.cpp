/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "FileUtil.h"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>

namespace XArkMidi {

bool ReadFileBytes(const std::wstring& path, std::vector<u8>& outData, std::string& outError) {
    outData.clear();
    outError.clear();

    FILE* file = nullptr;
#ifdef _WIN32
    _wfopen_s(&file, path.c_str(), L"rb");
#else
    // Convert wstring (UCS-4 on Linux) to UTF-8
    std::string utf8path;
    utf8path.reserve(path.size() * 4);
    for (wchar_t wc : path) {
        const uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80u) {
            utf8path += static_cast<char>(cp);
        } else if (cp < 0x800u) {
            utf8path += static_cast<char>(0xC0u | (cp >> 6));
            utf8path += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            utf8path += static_cast<char>(0xE0u | (cp >> 12));
            utf8path += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            utf8path += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else {
            utf8path += static_cast<char>(0xF0u | (cp >> 18));
            utf8path += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
            utf8path += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            utf8path += static_cast<char>(0x80u | (cp & 0x3Fu));
        }
    }
    file = std::fopen(utf8path.c_str(), "rb");
#endif
    if (!file) {
        outError = "Failed to open file";
        return false;
    }

#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END) != 0) {
#else
    if (std::fseek(file, 0, SEEK_END) != 0) {
#endif
        std::fclose(file);
        outError = "Failed to seek file";
        return false;
    }
    i64 size = 0;
#ifdef _WIN32
    size = static_cast<i64>(_ftelli64(file));
#else
    size = static_cast<i64>(std::ftell(file));
#endif
    if (size < 0) {
        std::fclose(file);
        outError = "Failed to get file size";
        return false;
    }
    if (static_cast<u64>(size) > static_cast<u64>(std::numeric_limits<size_t>::max()) ||
        static_cast<size_t>(size) > outData.max_size()) {
        std::fclose(file);
        throw std::bad_alloc();
    }
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_SET) != 0) {
#else
    if (std::fseek(file, 0, SEEK_SET) != 0) {
#endif
        std::fclose(file);
        outError = "Failed to rewind file";
        return false;
    }

    outData.resize(static_cast<size_t>(size));
    if (!outData.empty()) {
        size_t read = std::fread(outData.data(), 1, outData.size(), file);
        if (read != outData.size()) {
            std::fclose(file);
            outData.clear();
            outError = "Failed to read file";
            return false;
        }
    }

    std::fclose(file);
    return true;
}

} // namespace XArkMidi
