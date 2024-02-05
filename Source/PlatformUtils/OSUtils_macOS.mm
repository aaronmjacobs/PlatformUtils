#include "PlatformUtils/OSUtils.h"

#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/param.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace OSUtils
{
   std::optional<std::filesystem::path> getExecutablePath()
   {
      uint32_t size = MAXPATHLEN;
      char rawPath[size];
      if (_NSGetExecutablePath(rawPath, &size) == 0)
      {
         char realPath[size];
         if (realpath(rawPath, realPath))
         {
            return std::filesystem::path(realPath);
         }
      }

      return std::nullopt;
   }

   std::optional<std::filesystem::path> getKnownDirectoryPath(KnownDirectory knownDirectory)
   {
      if (knownDirectory == KnownDirectory::Home)
      {
         return std::filesystem::path([NSHomeDirectory() cStringUsingEncoding:NSUTF8StringEncoding]);
      }

      NSSearchPathDirectory searchPathDirectory = NSDesktopDirectory;
      NSSearchPathDomainMask domain = NSUserDomainMask;
      switch (knownDirectory)
      {
      case KnownDirectory::Desktop:
         searchPathDirectory = NSDesktopDirectory;
         domain = NSUserDomainMask;
         break;
      case KnownDirectory::Downloads:
         searchPathDirectory = NSDownloadsDirectory;
         domain = NSUserDomainMask;
         break;
      case KnownDirectory::UserApplications:
         searchPathDirectory = NSApplicationDirectory;
         domain = NSUserDomainMask;
         break;
      case KnownDirectory::CommonApplications:
         searchPathDirectory = NSApplicationDirectory;
         domain = NSLocalDomainMask;
         break;
      case KnownDirectory::UserApplicationData:
         searchPathDirectory = NSApplicationSupportDirectory;
         domain = NSUserDomainMask;
         break;
      case KnownDirectory::CommonApplicationData:
         searchPathDirectory = NSApplicationSupportDirectory;
         domain = NSLocalDomainMask;
         break;
      default:
         return std::nullopt;
      }

      if (NSURL* directoryURL = [[NSFileManager defaultManager] URLForDirectory:searchPathDirectory
                                                                inDomain:domain
                                                                appropriateForURL:nil
                                                                create:YES
                                                                error:nil])
      {
         return std::filesystem::path([[directoryURL path] cStringUsingEncoding:NSUTF8StringEncoding]);
      }

      return std::nullopt;
   }

   class DirectoryWatcher::Impl
   {
   public:
      Impl()
         : dispatchQueue(dispatch_queue_create("com.aaronmjacobs.PlatformUtils.DirectoryWatcher", DISPATCH_QUEUE_SERIAL))
      {
         dispatch_set_context(dispatchQueue, this);
         dispatch_set_finalizer_f(dispatchQueue, &finalizeQueue);
      }

      ~Impl()
      {
         idsByEventStream.clear();
         watches.clear();

         dispatch_release(dispatchQueue);

         {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this]() { return queueFinalized.load(); });
         }
      }

      void update()
      {
         std::vector<Notification> localNotifications;
         {
            std::lock_guard<std::mutex> lock(mutex);
            localNotifications = std::move(notifications);
            notifications = std::vector<Notification>{};
         }

         for (const Notification& notification : localNotifications)
         {
            auto idLocation = idsByEventStream.find(notification.eventStream);
            if (idLocation != idsByEventStream.end())
            {
               ID id = idLocation->second;

               auto watchLocation = watches.find(id);
               if (watchLocation != watches.end())
               {
                  Watch* watch = watchLocation->second.get();
                  watch->notify(notification.event, notification.path);
               }
            }
         }
      }

      ID addWatch(const std::filesystem::path& directory, bool recursive, NotifyFunction notifyFunction)
      {
         ID id = idCounter++;

         FSEventStreamContext context{};
         context.info = this;

         CFStringRef directoryString = CFStringCreateWithCString(nullptr, directory.string().c_str(), kCFStringEncodingUTF8);
         CFArrayRef pathsToWatch = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&directoryString), 1, nullptr);

         FSEventStreamRef eventStream = FSEventStreamCreate(nullptr, &streamCallback, &context, pathsToWatch, kFSEventStreamEventIdSinceNow, 0.0, kFSEventStreamCreateFlagFileEvents);
         FSEventStreamSetDispatchQueue(eventStream, dispatchQueue);

         CFRelease(pathsToWatch);
         CFRelease(directoryString);

         watches.emplace(id, std::make_unique<Watch>(id, directory, recursive, std::move(notifyFunction), eventStream));
         idsByEventStream.emplace(eventStream, id);

         return id;
      }

      void removeWatch(ID id)
      {
         auto location = watches.find(id);
         if (location != watches.end())
         {
            FSEventStreamRef eventStream = location->second->getEventStream();
            idsByEventStream.erase(eventStream);
            watches.erase(location);
         }
      }

   private:
      static void finalizeQueue(void* context)
      {
         Impl* self = static_cast<Impl*>(context);

         {
            std::lock_guard<std::mutex> lock(self->mutex);
            self->queueFinalized.store(true);
            self->cv.notify_all(); // Want to notify under the lock, as the cv will be destroyed when the thread wakes up
         }
      }

      static void streamCallback(ConstFSEventStreamRef streamRef, void* clientCallBackInfo, size_t numEvents, void* eventPaths, const FSEventStreamEventFlags* eventFlags, const FSEventStreamEventId* eventIds)
      {
         DirectoryWatcher::Impl* self = static_cast<DirectoryWatcher::Impl*>(clientCallBackInfo);
         std::lock_guard<std::mutex> lock(self->mutex);

         const char** eventPathStrings = static_cast<const char**>(eventPaths);
         for (size_t i = 0; i < numEvents; ++i)
         {
            DirectoryWatchEvent event = DirectoryWatchEvent::Create;
            if (eventFlags[i] & kFSEventStreamEventFlagItemCreated)
            {
               event = DirectoryWatchEvent::Create;
            }
            else if (eventFlags[i] & kFSEventStreamEventFlagItemRemoved)
            {
               event = DirectoryWatchEvent::Delete;
            }
            else if (eventFlags[i] & kFSEventStreamEventFlagItemRenamed)
            {
               event = DirectoryWatchEvent::Rename;
            }
            else if (eventFlags[i] & (kFSEventStreamEventFlagItemInodeMetaMod | kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemFinderInfoMod | kFSEventStreamEventFlagItemChangeOwner | kFSEventStreamEventFlagItemXattrMod))
            {
               event = DirectoryWatchEvent::Modify;
            }
            else
            {
               continue;
            }

            self->notifications.emplace_back(streamRef, event, eventPathStrings[i]);
         }
      }

      struct Notification
      {
         ConstFSEventStreamRef eventStream = nullptr;
         DirectoryWatchEvent event = DirectoryWatchEvent::Create;
         std::filesystem::path path;

         Notification() = default;
         Notification(ConstFSEventStreamRef stream, DirectoryWatchEvent watchEvent, const char* pathString)
            : eventStream(stream)
            , event(watchEvent)
            , path(pathString)
         {
         }
      };

      class Watch
      {
      public:
         Watch(ID idValue, const std::filesystem::path& dir, bool isRecursive, NotifyFunction&& notifyFunc, FSEventStreamRef stream)
            : id(idValue)
            , directory(dir)
            , recursive(isRecursive)
            , notifyFunction(std::move(notifyFunc))
            , eventStream(stream)
         {
            FSEventStreamStart(eventStream);
         }

         ~Watch()
         {
            FSEventStreamStop(eventStream);
            FSEventStreamInvalidate(eventStream);
            FSEventStreamRelease(eventStream);
         }

         FSEventStreamRef getEventStream() const
         {
            return eventStream;
         }

         void notify(DirectoryWatchEvent event, const std::filesystem::path& filePath) const
         {
            std::filesystem::path relativePath = filePath.lexically_relative(directory);
            if (!relativePath.empty() && (recursive || !relativePath.has_parent_path()))
            {
               notifyFunction(event, directory, relativePath);
            }
         }

      private:
         ID id = kInvalidIdentifier;
         std::filesystem::path directory;
         bool recursive = false;
         NotifyFunction notifyFunction;

         FSEventStreamRef eventStream = nullptr;
      };

      dispatch_queue_t dispatchQueue = nullptr;
      std::unordered_map<ID, std::unique_ptr<Watch>> watches;
      std::unordered_map<ConstFSEventStreamRef, ID> idsByEventStream;
      ID idCounter = 0;

      std::mutex mutex;
      std::condition_variable cv;
      std::vector<Notification> notifications;
      std::atomic_bool queueFinalized = { false };
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
