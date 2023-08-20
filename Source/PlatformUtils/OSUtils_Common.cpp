#include "PlatformUtils/OSUtils.h"

namespace OSUtils
{
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
