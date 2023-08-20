#include "PlatformUtils/OSUtils.h"

#include <bit>
#include <sstream>
#include <utility>
#include <vector>

#include <ShlObj.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace OSUtils
{
   namespace
   {
      std::string wstringToString(const std::wstring& wstring)
      {
         int numBytes = WideCharToMultiByte(CP_UTF8, 0, wstring.c_str(), wstring.size(), nullptr, 0, nullptr, nullptr);

         std::string string(numBytes, '\0');
         WideCharToMultiByte(CP_UTF8, 0, wstring.c_str(), wstring.size(), string.data(), numBytes, nullptr, nullptr);

         return string;
      }

      std::wstring stringToWstring(const std::string& string)
      {
         int numChars = MultiByteToWideChar(CP_UTF8, 0, string.c_str(), string.size(), nullptr, 0);

         std::wstring wstring(numChars, L'\0');
         MultiByteToWideChar(CP_UTF8, 0, string.c_str(), string.size(), wstring.data(), wstring.size());

         return wstring;
      }

      std::string readTextFromPipe(HANDLE handle)
      {
         LARGE_INTEGER fileSize{};
         if (GetFileSizeEx(handle, &fileSize) && fileSize.QuadPart > 0)
         {
            std::string text(fileSize.QuadPart, '\0');
            if (ReadFile(handle, text.data(), fileSize.QuadPart, nullptr, nullptr))
            {
               return text;
            }
         }

         return std::string{};
      }
   }

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

   std::unordered_map<std::string, std::string> getEnvironment()
   {
      std::unordered_map<std::string, std::string> environment;

      if (LPWCH environmentStrings = GetEnvironmentStringsW())
      {
         LPWCH itr = environmentStrings;
         while (*itr)
         {
            std::wstring envEntry = itr;
            std::size_t equalsIndex = envEntry.find('=');
            if (equalsIndex != 0 && equalsIndex != std::wstring::npos)
            {
               std::wstring key = envEntry.substr(0, equalsIndex);
               std::wstring value = envEntry.substr(equalsIndex + 1);
               environment.emplace(wstringToString(key), wstringToString(value));
            }

            itr += envEntry.size() + 1;
         }

         FreeEnvironmentStringsW(environmentStrings);
      }

      return environment;
   }

   std::optional<ProcessExitInfo> executeProcess(ProcessStartInfo startInfo)
   {
      std::wstring pathString = startInfo.path.wstring();

      std::wstringstream commandLineStream;
      commandLineStream << L'\"' << pathString << L'\"';
      for (const std::string& arg : startInfo.args)
      {
         commandLineStream << L" \"" << stringToWstring(arg) << L'\"';
      }
      std::wstring commandLine = commandLineStream.str();

      std::wstringstream environmentStream;
      if (startInfo.inheritEnvironment)
      {
         std::unordered_map<std::string, std::string> currentEnvironment = getEnvironment();

         for (const auto& [key, value] : currentEnvironment)
         {
            if (startInfo.env.count(key) == 0) // Provided environment variables take precedence
            {
               environmentStream << stringToWstring(key) << L"=" << stringToWstring(value) << L'\0';
            }
         }
      }
      for (const auto& [key, value] : startInfo.env)
      {
         environmentStream << stringToWstring(key) << L"=" << stringToWstring(value) << L'\0';
      }
      std::wstring environment = environmentStream.str();

      bool usePipes = startInfo.waitForExit && startInfo.readOutput;

      HANDLE hStdOutRead = nullptr;
      HANDLE hStdOutWrite = nullptr;
      HANDLE hStdErrRead = nullptr;
      HANDLE hStdErrWrite = nullptr;
      if (usePipes)
      {
         SECURITY_ATTRIBUTES securityAttributes{};
         securityAttributes.nLength = sizeof(securityAttributes);
         securityAttributes.bInheritHandle = true;

         if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &securityAttributes, 0))
         {
            return std::nullopt;
         }

         if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &securityAttributes, 0))
         {
            CloseHandle(hStdOutRead);
            CloseHandle(hStdOutWrite);
            return std::nullopt;
         }
      }

      STARTUPINFOW startupInfo{};
      startupInfo.cb = sizeof(startupInfo);
      if (usePipes)
      {
         startupInfo.dwFlags |= STARTF_USESTDHANDLES;
         startupInfo.hStdOutput = hStdOutWrite;
         startupInfo.hStdError = hStdErrWrite;
      }

      std::optional<ProcessExitInfo> exitInfo;
      PROCESS_INFORMATION processInformation{};
      if (CreateProcessW(pathString.c_str(), commandLine.data(), nullptr, nullptr, true, CREATE_UNICODE_ENVIRONMENT | DETACHED_PROCESS, environment.data(), nullptr, &startupInfo, &processInformation))
      {
         if (usePipes)
         {
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdErrWrite);
         }

         if (startInfo.waitForExit)
         {
            WaitForSingleObject(processInformation.hProcess, INFINITE);

            DWORD exitCode = 0;
            if (GetExitCodeProcess(processInformation.hProcess, &exitCode))
            {
               exitInfo = ProcessExitInfo{};
               exitInfo->exitCode = std::bit_cast<int>(exitCode);
            }
         }

         CloseHandle(processInformation.hProcess);
         CloseHandle(processInformation.hThread);
      }

      if (usePipes)
      {
         if (exitInfo)
         {
            exitInfo->stdOut = readTextFromPipe(hStdOutRead);
            exitInfo->stdErr = readTextFromPipe(hStdErrRead);
         }

         CloseHandle(hStdOutRead);
         CloseHandle(hStdErrRead);
      }

      return exitInfo;
   }
}
