#include "PlatformUtils/OSUtils.h"

#if defined(_WIN32)
#include <locale>
#include <vector>
#include <ShlObj.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif // defined(_WIN32)

#if defined(__linux__)
#include <linux/limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#endif // defined(__linux__)

namespace OSUtils
{
#if defined(_WIN32)
   std::optional<std::filesystem::path> getExecutablePath()
   {
      TCHAR buffer[MAX_PATH + 1];
      DWORD length = GetModuleFileName(nullptr, buffer, MAX_PATH);
      buffer[length] = '\0';

      if (length == 0 || length == MAX_PATH || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
      {
         static const DWORD kUnreasonablyLargeStringLength = 32767;
         std::vector<TCHAR> unreasonablyLargeBuffer(kUnreasonablyLargeStringLength + 1);
         length = GetModuleFileName(nullptr, unreasonablyLargeBuffer.data(), kUnreasonablyLargeStringLength);
         unreasonablyLargeBuffer[length] = '\0';

         if (length != 0 && length != kUnreasonablyLargeStringLength && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
         {
            return std::filesystem::path(unreasonablyLargeBuffer.data());
         }
      }
      else
      {
         return std::filesystem::path(buffer);
      }

      return std::nullopt;
   }

   std::optional<std::filesystem::path> getKnownDirectoryPath(KnownDirectory knownDirectory)
   {
      const KNOWNFOLDERID* folderID = nullptr;
      switch (knownDirectory)
      {
      case KnownDirectory::Home:
         folderID = &FOLDERID_Profile;
         break;
      case KnownDirectory::Desktop:
         folderID = &FOLDERID_Desktop;
         break;
      case KnownDirectory::Downloads:
         folderID = &FOLDERID_Downloads;
         break;
      case KnownDirectory::UserApplicationData:
         folderID = &FOLDERID_LocalAppData;
         break;
      case KnownDirectory::CommonApplicationData:
         folderID = &FOLDERID_ProgramData;
         break;
      default:
         break;
      }

      if (folderID == nullptr)
      {
         return std::nullopt;
      }

      std::optional<std::filesystem::path> knownDirectoryPath;
      PWSTR path = nullptr;
      if (SHGetKnownFolderPath(*folderID, KF_FLAG_DEFAULT, nullptr, &path) == S_OK)
      {
         knownDirectoryPath = std::filesystem::path(path);
      }
      CoTaskMemFree(path);

      return knownDirectoryPath;
   }
#endif // defined(_WIN32)

#if defined(__linux__)
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
#endif // defined(__linux__)

   bool setWorkingDirectoryToExecutableDirectory()
   {
      if (std::optional<std::filesystem::path> executablePath = getExecutablePath())
      {
         std::error_code errorCode;
         std::filesystem::current_path(executablePath->parent_path(), errorCode);

         return !errorCode;
      }

      return false;
   }
}
