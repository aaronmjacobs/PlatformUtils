#include "PlatformUtils/OSUtils.h"

#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/param.h>

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
         return std::filesystem::path([NSHomeDirectory() cStringUsingEncoding:NSASCIIStringEncoding]);
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
         return std::filesystem::path([[directoryURL path] cStringUsingEncoding:NSASCIIStringEncoding]);
      }

      return std::nullopt;
   }
}
