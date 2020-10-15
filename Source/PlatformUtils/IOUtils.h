#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace IOUtils
{
   std::optional<std::string> readTextFile(const std::filesystem::path& path);
   std::optional<std::vector<uint8_t>> readBinaryFile(const std::filesystem::path& path);

   bool writeTextFile(const std::filesystem::path& path, std::string_view data);
   bool writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& data);

   std::optional<std::filesystem::path> findProjectDirectory();

   std::optional<std::filesystem::path> getAbsolutePath(const std::filesystem::path& base, const std::filesystem::path& relativePath);
   std::optional<std::filesystem::path> getAboluteProjectPath(const std::filesystem::path& relativePath);
   std::optional<std::filesystem::path> getAbsoluteAppDataPath(std::string_view appName, const std::filesystem::path& relativePath);
}
