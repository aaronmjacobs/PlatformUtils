#include "PlatformUtils/OSUtils.h"

#if defined(_WIN32)
#include <bit>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>
#include <ShlObj.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif // defined(_WIN32)

#if defined(__linux__)
#include <linux/limits.h>
#include <pwd.h>
#include <sys/types.h>
#endif // defined(__linux__)

#if defined(__linux__) || defined(__APPLE__)
#include <sstream>
#include <string>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
extern char** environ;
#endif // defined(__linux__) || defined(__APPLE__)

namespace OSUtils
{
#if defined(_WIN32)
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

#if defined(__linux__) || defined(__APPLE__)
   std::unordered_map<std::string, std::string> getEnvironment()
   {
      std::unordered_map<std::string, std::string> environment;

      for (char** itr = environ; *itr; ++itr)
      {
         std::string envEntry = *itr;
         std::size_t equalsIndex = envEntry.find('=');
         if (equalsIndex != 0 && equalsIndex != std::string::npos)
         {
            environment.emplace(envEntry.substr(0, equalsIndex), envEntry.substr(equalsIndex + 1));
         }
      }

      return environment;
   }

   std::optional<ProcessExitInfo> executeProcess(ProcessStartInfo startInfo)
   {
      bool usePipes = startInfo.waitForExit && startInfo.readOutput;

      int outPipe[2]{};
      int errPipe[2]{};
      if (usePipes)
      {
         pipe(outPipe);
         pipe(errPipe);
      }

      pid_t pid = fork();
      if (pid == 0)
      {
         // Child process

         if (usePipes)
         {
            // Redirect stdout and stderr
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);

            close(outPipe[0]);
            close(outPipe[1]);
            close(errPipe[0]);
            close(errPipe[1]);
         }

         std::string pathString = startInfo.path.string();

         std::vector<char*> args;
         args.reserve(startInfo.args.size() + 2);
         args.push_back(pathString.data());
         for (std::string& arg : startInfo.args)
         {
            args.push_back(arg.data());
         }
         args.push_back(nullptr);

         std::vector<std::string> envStrings;
         if (startInfo.inheritEnvironment)
         {
            std::unordered_map<std::string, std::string> currentEnvironment = getEnvironment();
            envStrings.reserve(currentEnvironment.size() + startInfo.env.size());

            for (const auto& [key, value] : currentEnvironment)
            {
               if (startInfo.env.count(key) == 0) // Provided environment variables take precedence
               {
                  envStrings.push_back(key + "=" + value);
               }
            }
         }
         else
         {
            envStrings.reserve(startInfo.env.size());
         }

         for (const auto& [key, value] : startInfo.env)
         {
            envStrings.push_back(key + "=" + value);
         }

         std::vector<char*> env;
         if (!envStrings.empty())
         {
            env.reserve(envStrings.size() + 1);
            for (std::string& envString : envStrings)
            {
               env.push_back(envString.data());
            }
            env.push_back(nullptr);
         }

         fflush(stdout);
         fflush(stderr);

         const char* file = pathString.c_str();
         char* const* argv = args.data();
         char* const* envp = env.empty() ? nullptr : env.data();

         execve(file, argv, envp);

         int error = errno;
         fprintf(stderr, "Exec failed with errno = %d (%s)", error, strerror(error));
         abort();
      }

      // Parent process

      if (usePipes)
      {
         close(outPipe[1]);
         close(errPipe[1]);
      }

      std::optional<ProcessExitInfo> exitInfo;
      if (startInfo.waitForExit)
      {
         int status = 0;
         waitpid(pid, &status, 0);

         if (WIFEXITED(status))
         {
            exitInfo = ProcessExitInfo{};
            exitInfo->exitCode = WEXITSTATUS(status);
         }
      }

      if (usePipes)
      {
         if (exitInfo)
         {
            struct stat outStat{};
            struct stat errStat{};

            fstat(outPipe[0], &outStat);
            fstat(errPipe[0], &errStat);

            exitInfo->stdOut.resize(outStat.st_size);
            read(outPipe[0], exitInfo->stdOut.data(), outStat.st_size);

            exitInfo->stdErr.resize(errStat.st_size);
            read(errPipe[0], exitInfo->stdErr.data(), errStat.st_size);
         }

         close(outPipe[0]);
         close(errPipe[0]);
      }

      return exitInfo;
   }
#endif // defined(__linux__) || defined(__APPLE__)

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
