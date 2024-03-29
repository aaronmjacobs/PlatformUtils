#include "PlatformUtils/IOUtils.h"

#include "PlatformUtils/OSUtils.h"

#include <fstream>

namespace IOUtils
{
   std::optional<std::string> readTextFile(const std::filesystem::path& path)
   {
      if (std::filesystem::is_regular_file(path))
      {
         std::ifstream in(path);
         if (in)
         {
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
         }
      }

      return std::nullopt;
   }

   std::optional<std::vector<uint8_t>> readBinaryFile(const std::filesystem::path& path)
   {
      if (std::filesystem::is_regular_file(path))
      {
         std::ifstream in(path, std::ifstream::binary);
         if (in)
         {
            std::streampos start = in.tellg();
            in.seekg(0, std::ios_base::end);
            std::streamoff size = in.tellg() - start;

            if (size > 0)
            {
               in.seekg(0, std::ios_base::beg);

               std::vector<uint8_t> data(static_cast<size_t>(size));
               in.read(reinterpret_cast<char*>(data.data()), size);

               return data;
            }
         }
      }

      return std::nullopt;
   }

   bool writeTextFile(const std::filesystem::path& path, std::string_view data)
   {
      if (path.has_filename())
      {
         std::error_code errorCode;
         std::filesystem::create_directories(path.parent_path(), errorCode);
         if (!errorCode)
         {
            std::ofstream out(path);
            if (out)
            {
               out << data;
               return true;
            }
         }
      }

      return false;
   }

   bool writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& data)
   {
      if (path.has_filename())
      {
         std::error_code errorCode;
         std::filesystem::create_directories(path.parent_path(), errorCode);
         if (!errorCode)
         {
            std::ofstream out(path, std::ofstream::binary);
            if (out)
            {
               out.write(reinterpret_cast<const char*>(data.data()), data.size());
               return true;
            }
         }
      }

      return false;
   }

   std::optional<std::filesystem::path> findProjectDirectory()
   {
      static std::optional<std::filesystem::path> cachedProjectDirectory;

      if (cachedProjectDirectory)
      {
         return cachedProjectDirectory;
      }

      if (std::optional<std::filesystem::path> executeablePath = OSUtils::getExecutablePath())
      {
         static const char* kCMakeListsName = "CMakeLists.txt";
         static const int kNumDirectoriesToClimb = 2;

         // The project directory location depends on the build / install environment
         std::filesystem::path executableDirectory = executeablePath->parent_path();

         // First, look for CMakeLists.txt
         std::filesystem::path potentialProjectDirectory = executableDirectory;
         for (int i = 0; i <= kNumDirectoriesToClimb; ++i)
         {
            if (std::filesystem::is_directory(potentialProjectDirectory))
            {
               std::filesystem::path potentialCMakeListsPath = potentialProjectDirectory / kCMakeListsName;
               if (std::filesystem::is_regular_file(potentialCMakeListsPath))
               {
                  std::error_code errorCode;
                  std::filesystem::path canonicalProjectDirectory = std::filesystem::canonical(potentialProjectDirectory, errorCode);
                  if (!errorCode)
                  {
                     cachedProjectDirectory = canonicalProjectDirectory;
                     break;
                  }
               }

               potentialProjectDirectory = potentialProjectDirectory.parent_path();
            }
         }

         // If it can't be found, assume the project has been installed, and the project directory is the same as the executable directory
         if (!cachedProjectDirectory && std::filesystem::is_directory(executableDirectory))
         {
            std::error_code errorCode;
            std::filesystem::path canonicalProjectDirectory = std::filesystem::canonical(executableDirectory, errorCode);
            if (!errorCode)
            {
               cachedProjectDirectory = canonicalProjectDirectory;
            }
         }
      }

      return cachedProjectDirectory;
   }

   std::optional<std::filesystem::path> getAbsolutePath(const std::filesystem::path& base, const std::filesystem::path& relativePath)
   {
      if (base.is_absolute() && relativePath.is_relative())
      {
         std::error_code errorCode;
         std::filesystem::path canonicalAbsolutePath = std::filesystem::weakly_canonical(base / relativePath, errorCode);
         if (!errorCode)
         {
            return canonicalAbsolutePath;
         }
      }

      return std::nullopt;
   }

   std::optional<std::filesystem::path> getAbsoluteKnownPath(OSUtils::KnownDirectory knownDirectory, const std::filesystem::path& relativePath)
   {
      if (std::optional<std::filesystem::path> knownDirectoryPath = OSUtils::getKnownDirectoryPath(knownDirectory))
      {
         return getAbsolutePath(*knownDirectoryPath, relativePath);
      }

      return std::nullopt;
   }

   std::optional<std::filesystem::path> getAboluteProjectPath(const std::filesystem::path& relativePath)
   {
      if (std::optional<std::filesystem::path> projectDirectory = findProjectDirectory())
      {
         return getAbsolutePath(*projectDirectory, relativePath);
      }

      return std::nullopt;
   }

   std::optional<std::filesystem::path> getAbsoluteAppDataPath(std::string_view appName, const std::filesystem::path& relativePath)
   {
      return getAbsoluteKnownPath(OSUtils::KnownDirectory::UserApplicationData, appName / relativePath);
   }

   std::optional<std::filesystem::path> getAbsoluteCommonAppDataPath(std::string_view appName, const std::filesystem::path& relativePath)
   {
      return getAbsoluteKnownPath(OSUtils::KnownDirectory::CommonApplicationData, appName / relativePath);
   }
}
