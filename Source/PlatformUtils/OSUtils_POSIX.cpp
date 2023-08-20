#include "PlatformUtils/OSUtils.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace OSUtils
{
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
}
