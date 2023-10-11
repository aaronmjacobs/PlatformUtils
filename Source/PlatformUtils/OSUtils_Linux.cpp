#include "PlatformUtils/OSUtils.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <linux/limits.h>
#include <poll.h>
#include <pwd.h>
#include <sys/inotify.h>
#include <sys/param.h>
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

   class DirectoryWatcher::Impl
   {
   public:
      Impl()
         : eventQueue(inotify_init1(IN_NONBLOCK | IN_CLOEXEC))
      {
      }

      ~Impl()
      {
         idsByDescriptor.clear();
         watches.clear();

         close(eventQueue);
      }

      void update()
      {
         std::vector<Notification> notifications;

         pollfd pollData{};
         pollData.fd = eventQueue;
         pollData.events = POLLIN;

         int numSet = ::poll(&pollData, 1, 0);
         if (numSet <= 0 || !(pollData.revents & pollData.events) || (pollData.revents & (POLLERR | POLLHUP | POLLNVAL)))
         {
            return;
         }

         while (true)
         {
            std::array<uint8_t, sizeof(inotify_event) + MAXPATHLEN + 1> buffer{};
            
            ssize_t length = ::read(eventQueue, buffer.data(), buffer.size());
            if (length <= 0)
            {
               break;
            }

            ssize_t offset = 0;
            while (offset < length)
            {
               const inotify_event* event = reinterpret_cast<const inotify_event*>(&buffer[offset]);
               offset += sizeof(inotify_event) + event->len;

               auto idLocation = idsByDescriptor.find(event->wd);
               if (idLocation == idsByDescriptor.end())
               {
                  continue;
               }

               Notification notification;
               notification.id = idLocation->second;
               notification.path = event->name;
               notification.descriptor = event->wd;

               if (event->mask & IN_CREATE)
               {
                  notification.event = DirectoryWatchEvent::Create;
               }
               else if (event->mask & IN_DELETE) // DELETE_SELF handled via recursion (except for top level directory)
               {
                  notification.event = DirectoryWatchEvent::Delete;
               }
               else if (event->mask & (IN_ATTRIB | IN_MODIFY))
               {
                  notification.event = DirectoryWatchEvent::Modify;
               }
               else if (event->mask & (IN_MOVED_FROM | IN_MOVED_TO))
               {
                  notification.event = DirectoryWatchEvent::Rename;
               }
               else
               {
                  continue;
               }

               // Don't add duplicate notifications
               if (std::find(notifications.begin(), notifications.end(), notification) == notifications.end())
               {
                  notifications.emplace_back(std::move(notification));
               }
            }
         }

         for (const Notification& notification : notifications)
         {
            auto location = watches.find(notification.id);
            if (location != watches.end())
            {
               Watch& watch = *location->second;
               watch.notify(notification.descriptor, notification.event, notification.path);
            }
         }
      }

      ID addWatch(const std::filesystem::path& directory, bool recursive, NotifyFunction notifyFunction)
      {
         ID id = idCounter++;
         auto location = watches.emplace(id, std::make_unique<Watch>(*this, id, directory, std::move(notifyFunction), recursive, eventQueue)).first;
         Watch& watch = *location->second;

         if (watch.getDescriptors().empty())
         {
            watches.erase(location);
            --idCounter;
            return kInvalidIdentifier;
         }

         return id;
      }

      void removeWatch(ID id)
      {
         watches.erase(id);
      }

      void registerDescriptor(ID id, int descriptor)
      {
         idsByDescriptor.emplace(descriptor, id);
      }

      void unregisterDescriptor(int descriptor)
      {
         idsByDescriptor.erase(descriptor);
      }

   private:
      struct Notification
      {
         ID id = kInvalidIdentifier;
         DirectoryWatchEvent event = DirectoryWatchEvent::Create;
         std::filesystem::path path;
         int descriptor = -1;

         bool operator==(const Notification& other) const = default;
      };

      class Watch
      {
      public:
         Watch(Impl& owningImpl, ID idValue, const std::filesystem::path& dir, NotifyFunction&& notifyFunc, bool isRecursive, int eventQueueValue)
            : impl(owningImpl)
            , id(idValue)
            , notifyFunction(notifyFunc)
            , recursive(isRecursive)
            , eventQueue(eventQueueValue)
         {
            if (add(dir) && recursive)
            {
               for (const std::filesystem::path& subPath : std::filesystem::recursive_directory_iterator(dir))
               {
                  add(subPath);
               }
            }
         }

         ~Watch()
         {
            for (const auto& [descriptor, directory] : directoriesByDescriptor)
            {
               inotify_rm_watch(eventQueue, descriptor);
            }
         }

         void notify(int descriptor, DirectoryWatchEvent event, const std::filesystem::path& filePath)
         {
            auto directoryLocation = directoriesByDescriptor.find(descriptor);
            if (directoryLocation != directoriesByDescriptor.end())
            {
               std::filesystem::path directory = directoryLocation->second; // Need to make a copy, as we may modify the map below (including removing this entry)

               if (recursive)
               {
                  std::filesystem::path absolutePath = directory / filePath;

                  if (event == DirectoryWatchEvent::Delete || event == DirectoryWatchEvent::Rename)
                  {
                     // Remove all directories matching subdir
                     std::vector<std::filesystem::path> dirsToRemove;
                     for (const auto& [descriptor, directory] : directoriesByDescriptor)
                     {
                        std::filesystem::path potentialDir = (absolutePath / directory).lexically_normal();
                        auto [absolutePathLocation, _] = std::mismatch(absolutePath.begin(), absolutePath.end(), potentialDir.begin(), potentialDir.end());
                        if (absolutePathLocation == absolutePath.end())
                        {
                           dirsToRemove.push_back(potentialDir);
                        }
                     }

                     for (const std::filesystem::path& dir : dirsToRemove)
                     {
                        remove(dir);
                     }
                  }

                  if (event == DirectoryWatchEvent::Create || event == DirectoryWatchEvent::Rename)
                  {
                     if (add(absolutePath))
                     {
                        for (const std::filesystem::path& subPath : std::filesystem::recursive_directory_iterator(absolutePath))
                        {
                           add(subPath);
                        }
                     }
                  }
               }

               notifyFunction(event, directory, filePath);
            }
         }

         const std::unordered_map<int, std::filesystem::path>& getDescriptors() const
         {
            return directoriesByDescriptor;
         }

      private:
         static const uint32_t kMask = IN_ATTRIB | IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO;

         bool add(const std::filesystem::path& directory)
         {
            if (!std::filesystem::is_directory(directory))
            {
               return false;
            }

            int descriptor = inotify_add_watch(eventQueue, directory.c_str(), kMask);
            if (descriptor >= 0)
            {
               directoriesByDescriptor.emplace(descriptor, directory);
               descriptorsByDirectory.emplace(directory, descriptor);

               impl.registerDescriptor(id, descriptor);

               return true;
            }

            return false;
         }

         void remove(const std::filesystem::path& directory)
         {
            auto location = descriptorsByDirectory.find(directory);
            if (location != descriptorsByDirectory.end())
            {
               int descriptor = location->second;
               inotify_rm_watch(eventQueue, descriptor);

               descriptorsByDirectory.erase(location);
               directoriesByDescriptor.erase(descriptor);

               impl.unregisterDescriptor(descriptor);
            }
         }

         Impl& impl;

         ID id = kInvalidIdentifier;
         NotifyFunction notifyFunction;
         bool recursive = false;

         int eventQueue = -1;
         std::unordered_map<int, std::filesystem::path> directoriesByDescriptor;
         std::unordered_map<std::filesystem::path, int> descriptorsByDirectory;
      };

      std::unordered_map<ID, std::unique_ptr<Watch>> watches;
      std::unordered_map<int, ID> idsByDescriptor;
      ID idCounter = 0;
      int eventQueue = -1;
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
