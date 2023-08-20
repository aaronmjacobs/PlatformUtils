#include "PlatformUtils/OSUtils.h"

#include <linux/limits.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace OSUtils
{
   std::optional<std::filesystem::path> getExecutablePath()
   {
      char path[PATH_MAX + 1];
      ssize_t numBytes = readlink("/proc/self/exe", path, PATH_MAX);
      if (numBytes >= 0 && numBytes <= PATH_MAX)
      {
         path[numBytes] = '\0';
         return std::filesystem::path(path);
      }

      return std::nullopt;
   }

   namespace
   {
      const char* getHomeDir()
      {
         // First, check the HOME environment variable
         char* homeDir = secure_getenv("HOME");
         if (homeDir)
         {
            return homeDir;
         }

         // If it isn't set, grab the directory from the password entry file
         if (struct passwd* pw = getpwuid(getuid()))
         {
            return pw->pw_dir;
         }

         return nullptr;
      }
   }

   std::optional<std::filesystem::path> getKnownDirectoryPath(KnownDirectory knownDirectory)
   {
      if (knownDirectory == KnownDirectory::CommonApplicationData)
      {
         return "/var/lib";
      }

      const char* homeDir = getHomeDir();
      if (!homeDir)
      {
         return std::nullopt;
      }

      std::filesystem::path homePath = homeDir;
      switch (knownDirectory)
      {
      case KnownDirectory::Home:
         return homePath;
      case KnownDirectory::Desktop:
         return homePath / "Desktop";
      case KnownDirectory::Downloads:
         return homePath / "Downloads";
      case KnownDirectory::UserApplicationData:
         return homePath / ".config";
      default:
         return std::nullopt;
      }
   }
}
