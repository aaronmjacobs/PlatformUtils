#pragma once

#include <filesystem>
#include <functional>
#include <memory>
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

   std::optional<std::filesystem::path> getExecutablePath();
   std::optional<std::filesystem::path> getKnownDirectoryPath(KnownDirectory knownDirectory);
   bool setWorkingDirectoryToExecutableDirectory();

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

   std::unordered_map<std::string, std::string> getEnvironment();
   std::optional<ProcessExitInfo> executeProcess(ProcessStartInfo startInfo);

   enum class DirectoryWatchEvent
   {
      Create,
      Delete,
      Rename,
      Modify
   };

   class DirectoryWatcher
   {
   public:
      using ID = int;
      using NotifyFunction = std::function<void(DirectoryWatchEvent, const std::filesystem::path& /* directory */, const std::filesystem::path& /* file */)>;

      static constexpr ID kInvalidIdentifier = -1;

      DirectoryWatcher();
      ~DirectoryWatcher();

      void update();

      ID addWatch(const std::filesystem::path& directory, bool recursive, NotifyFunction notifyFunction);
      void removeWatch(ID id);

   private:
      class Impl;
      std::unique_ptr<Impl> impl;
   };
}
