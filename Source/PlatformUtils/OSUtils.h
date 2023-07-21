#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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

   struct ProcessStartInfo
   {
      std::filesystem::path path;
      std::vector<std::string> args;
      std::unordered_map<std::string, std::string> env;

      bool inheritEnvironment = true;
      bool waitForExit = true;
      bool readOutput = false;
   };

   struct ProcessExitInfo
   {
      int exitCode = 0;

      std::string stdOut;
      std::string stdErr;
   };

   std::optional<std::filesystem::path> getExecutablePath();
   std::optional<std::filesystem::path> getKnownDirectoryPath(KnownDirectory knownDirectory);

   std::unordered_map<std::string, std::string> getEnvironment();
   std::optional<ProcessExitInfo> executeProcess(ProcessStartInfo startInfo);

   bool setWorkingDirectoryToExecutableDirectory();
}
