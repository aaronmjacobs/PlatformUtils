#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace OSUtils
{
   enum class KnownDirectory
   {
      Home,
      Desktop,
      Downloads,

      UserApplicationData,
      CommonApplicationData,
   };

   std::optional<std::filesystem::path> getExecutablePath();
   std::optional<std::filesystem::path> getKnownDirectoryPath(KnownDirectory knownDirectory);

   bool setWorkingDirectoryToExecutableDirectory();
}
