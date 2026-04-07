#pragma once
#include "Types.h"
#include <string>
#include <vector>

namespace XArkMidi {

bool ReadFileBytes(const std::wstring& path, std::vector<u8>& outData, std::string& outError);

} // namespace XArkMidi

