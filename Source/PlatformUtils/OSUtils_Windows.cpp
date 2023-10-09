#include "PlatformUtils/OSUtils.h"

#include <array>
#include <bit>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

#if !defined(NOMINMAX)
#  define NOMINMAX
#endif

#if !defined(WIN32_LEAN_AND_MEAN)
#  define WIN32_LEAN_AND_MEAN
#endif

#include <ShlObj.h>
#include <Windows.h>

namespace OSUtils
{
   namespace
   {
      std::string wstringToString(const std::wstring& wstring)
      {
         int numWideChars = wstring.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) ? static_cast<int>(wstring.size()) : -1;
         int numBytes = WideCharToMultiByte(CP_UTF8, 0, wstring.c_str(), numWideChars, nullptr, 0, nullptr, nullptr);

         std::string string(numBytes, '\0');
         WideCharToMultiByte(CP_UTF8, 0, wstring.c_str(), numWideChars, string.data(), numBytes, nullptr, nullptr);

         return string;
      }

      std::wstring stringToWstring(const std::string& string)
      {
         int numBytes = string.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) ? static_cast<int>(string.size()) : -1;
         int numWideChars = MultiByteToWideChar(CP_UTF8, 0, string.c_str(), numBytes, nullptr, 0);

         std::wstring wstring(numWideChars, L'\0');
         MultiByteToWideChar(CP_UTF8, 0, string.c_str(), numBytes, wstring.data(), numWideChars);

         return wstring;
      }

      std::string readTextFromPipe(HANDLE handle)
      {
         LARGE_INTEGER fileSize{};
         if (GetFileSizeEx(handle, &fileSize) && fileSize.LowPart > 0 && fileSize.HighPart == 0)
         {
            std::string text(fileSize.LowPart, '\0');
            if (ReadFile(handle, text.data(), fileSize.LowPart, nullptr, nullptr))
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

      PROCESS_INFORMATION processInformation{};
      bool processCreated = CreateProcessW(pathString.c_str(), commandLine.data(), nullptr, nullptr, true, CREATE_UNICODE_ENVIRONMENT | DETACHED_PROCESS, environment.data(), nullptr, &startupInfo, &processInformation);

      if (usePipes)
      {
         CloseHandle(hStdOutWrite);
         CloseHandle(hStdErrWrite);
      }

      std::optional<ProcessExitInfo> exitInfo;
      if (processCreated)
      {
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

   class DirectoryWatcher::Impl
   {
   public:
      void update()
      {
         std::vector<Notification> notifications;
         for (auto& [id, watch] : watches)
         {
            while (watch->poll(notifications));
         }

         for (const Notification& notification : notifications)
         {
            auto location = watches.find(notification.id);
            if (location != watches.end())
            {
               const Watch& watch = *location->second;
               watch.notify(notification.event, notification.path);
            }
         }
      }

      ID addWatch(const std::filesystem::path& directory, bool recursive, NotifyFunction notifyFunction)
      {
         HANDLE directoryHandle = CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
         if (directoryHandle == INVALID_HANDLE_VALUE)
         {
            return kInvalidID;
         }

         ID id = idCounter++;
         auto itr = watches.emplace(id, std::make_unique<Watch>(id, directory, std::move(notifyFunction), recursive, directoryHandle)).first;
         Watch& watch = *itr->second;

         if (!watch.refresh())
         {
            watches.erase(id);
            --idCounter;
            return kInvalidID;
         }

         return id;
      }

      void removeWatch(ID id)
      {
         watches.erase(id);
      }

   private:
      struct Notification
      {
         ID id = kInvalidID;
         DirectoryWatchEvent event = DirectoryWatchEvent::Create;
         std::filesystem::path path;

         bool operator==(const Notification& other) const = default;
      };

      class Watch
      {
      public:
         Watch(ID idValue, const std::filesystem::path& dir, NotifyFunction&& notifyFunc, bool isRecursive, HANDLE dirHandle)
            : id(idValue)
            , directory(dir)
            , notifyFunction(std::move(notifyFunc))
            , recursive(isRecursive)
            , directoryHandle(dirHandle)
         {
            overlapped.hEvent = CreateEvent(nullptr, false, false, nullptr);
         }

         ~Watch()
         {
            if (directoryHandle)
            {
               CancelIo(directoryHandle);
               CloseHandle(directoryHandle);
            }

            if (overlapped.hEvent)
            {
               if (!HasOverlappedIoCompleted(&overlapped))
               {
                  // Should not need to wait long since CancelIo() was called
                  WaitForSingleObject(overlapped.hEvent, INFINITE);
               }

               CloseHandle(overlapped.hEvent);
            }
         }

         bool refresh()
         {
            static const DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SECURITY;

            return ReadDirectoryChangesW(directoryHandle, buffer.data(), static_cast<DWORD>(buffer.size()), recursive, kFilter, nullptr, &overlapped, nullptr);
         }

         bool poll(std::vector<Notification>& notifications)
         {
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 0);
            if (waitResult == WAIT_OBJECT_0)
            {
               DWORD numBytesTransferred = 0;
               if (GetOverlappedResult(directoryHandle, &overlapped, &numBytesTransferred, false))
               {
                  DWORD bufferOffset = 0;
                  while (true)
                  {
                     Notification notification;
                     notification.id = id;

                     FILE_NOTIFY_INFORMATION* event = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data() + bufferOffset);
                     DWORD fileNameLength = event->FileNameLength / sizeof(wchar_t);
                     notification.path = std::filesystem::path(event->FileName, event->FileName + fileNameLength);

                     switch (event->Action)
                     {
                     case FILE_ACTION_ADDED:
                        notification.event = DirectoryWatchEvent::Create;
                        break;
                     case FILE_ACTION_REMOVED:
                        notification.event = DirectoryWatchEvent::Delete;
                        break;
                     case FILE_ACTION_MODIFIED:
                        notification.event = DirectoryWatchEvent::Modify;
                        break;
                     case FILE_ACTION_RENAMED_OLD_NAME:
                        notification.event = DirectoryWatchEvent::Rename;
                        break;
                     case FILE_ACTION_RENAMED_NEW_NAME:
                        notification.event = DirectoryWatchEvent::Rename;
                        break;
                     default:
                        break;
                     }

                     // Don't add duplicate notifications (Windows can provide two events for the same change, due to filesystem quirks)
                     if (std::find(notifications.begin(), notifications.end(), notification) == notifications.end())
                     {
                        notifications.emplace_back(std::move(notification));
                     }

                     if (event->NextEntryOffset)
                     {
                        bufferOffset += event->NextEntryOffset;
                     }
                     else
                     {
                        break;
                     }
                  }
               }

               refresh();

               return true;
            }

            return false;
         }

         void notify(DirectoryWatchEvent event, const std::filesystem::path& filePath) const
         {
            notifyFunction(event, directory, filePath);
         }

         private:
            ID id = kInvalidID;
            std::filesystem::path directory;
            NotifyFunction notifyFunction;
            bool recursive = false;

            alignas(DWORD) std::array<uint8_t, 32 * 1024> buffer{};
            HANDLE directoryHandle = nullptr;
            OVERLAPPED overlapped{};
      };

      std::unordered_map<ID, std::unique_ptr<Watch>> watches;
      ID idCounter = 0;
   };

   DirectoryWatcher::DirectoryWatcher()
      : impl(std::make_unique<Impl>())
   {
   }

   DirectoryWatcher::~DirectoryWatcher()
   {
   }

   void DirectoryWatcher::update()
   {
      impl->update();
   }

   DirectoryWatcher::ID DirectoryWatcher::addWatch(const std::filesystem::path& directory, bool recursive, DirectoryWatcher::NotifyFunction notifyFunction)
   {
      return impl->addWatch(directory, recursive, std::move(notifyFunction));
   }

   void DirectoryWatcher::removeWatch(DirectoryWatcher::ID id)
   {
      impl->removeWatch(id);
   }
}
