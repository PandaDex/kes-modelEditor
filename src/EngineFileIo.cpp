#include "asset_file_io.h"

#include <filesystem>
#include <fstream>
#include <iterator>

// loose files only, no pak lookup
bool LoadAssetFileBytes(const char* path, std::vector<uint8_t>& outData, std::string* outError)
{
    outData.clear();
    std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path ? path : "")), std::ios::binary);
    if (!file)
    {
        if (outError)
            *outError = std::string("Cannot open ") + (path ? path : "<null>");
        return false;
    }
    outData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool LoadAssetFileText(const char* path, std::string& outText, std::string* outError)
{
    std::vector<uint8_t> bytes;
    if (!LoadAssetFileBytes(path, bytes, outError))
        return false;
    outText.assign(bytes.begin(), bytes.end());
    return true;
}
