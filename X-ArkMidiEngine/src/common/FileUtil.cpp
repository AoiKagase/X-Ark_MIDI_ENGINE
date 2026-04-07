#include "FileUtil.h"
#include <cstdio>

namespace XArkMidi {

bool ReadFileBytes(const std::wstring& path, std::vector<u8>& outData, std::string& outError) {
    outData.clear();
    outError.clear();

    FILE* file = nullptr;
#ifdef _WIN32
    _wfopen_s(&file, path.c_str(), L"rb");
#else
    file = std::fopen(std::string(path.begin(), path.end()).c_str(), "rb");
#endif
    if (!file) {
        outError = "Failed to open file";
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        outError = "Failed to seek file";
        return false;
    }
    long size = std::ftell(file);
    if (size < 0) {
        std::fclose(file);
        outError = "Failed to get file size";
        return false;
    }
    if (std::fseek(file, 0, SEEK_SET) != 0) {
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

