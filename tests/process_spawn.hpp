#ifndef BLOCKCHAIN_TESTS_PROCESS_SPAWN_HPP
#define BLOCKCHAIN_TESTS_PROCESS_SPAWN_HPP

// Minimal cross-platform child process helper for multi-process integration tests.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace bctest {

struct ChildProcess {
#ifdef _WIN32
  PROCESS_INFORMATION info{};
  bool valid() const noexcept { return info.hProcess != nullptr; }
#else
  pid_t pid = -1;
  bool valid() const noexcept { return pid > 0; }
#endif
};

inline std::string quote_arg(const std::string& arg) {
#ifdef _WIN32
  if (arg.find_first_of(" \t\"") == std::string::npos) {
    return arg;
  }
  std::string quoted = "\"";
  for (char ch : arg) {
    if (ch == '"') {
      quoted += "\\\"";
    } else {
      quoted += ch;
    }
  }
  quoted += '"';
  return quoted;
#else
  if (arg.find_first_of(" \t'\"\\") == std::string::npos) {
    return arg;
  }
  std::string quoted = "'";
  for (char ch : arg) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += '\'';
  return quoted;
#endif
}

inline std::string join_command(const std::vector<std::string>& args) {
  std::string cmd;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      cmd.push_back(' ');
    }
    cmd += quote_arg(args[i]);
  }
  return cmd;
}

inline ChildProcess spawn_process(const std::vector<std::string>& args) {
  ChildProcess child{};
  if (args.empty()) {
    return child;
  }

#ifdef _WIN32
  std::string cmd = join_command(args);
  std::vector<char> buffer(cmd.begin(), cmd.end());
  buffer.push_back('\0');

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  if (CreateProcessA(args[0].c_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                     &startup, &child.info) == FALSE) {
    child.info = PROCESS_INFORMATION{};
  }
#else
  child.pid = fork();
  if (child.pid == 0) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execv(args[0].c_str(), argv.data());
    _exit(127);
  }
#endif
  return child;
}

inline bool wait_for_file(const std::filesystem::path& path,
                          std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path)) {
      std::ifstream in(path);
      std::uint16_t port = 0;
      if (in >> port && port != 0) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

inline std::uint16_t read_port_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::uint32_t port = 0;
  in >> port;
  return static_cast<std::uint16_t>(port);
}

inline int wait_process(ChildProcess& child) {
  if (!child.valid()) {
    return -1;
  }

#ifdef _WIN32
  WaitForSingleObject(child.info.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(child.info.hProcess, &exit_code);
  CloseHandle(child.info.hThread);
  CloseHandle(child.info.hProcess);
  child.info = PROCESS_INFORMATION{};
  return static_cast<int>(exit_code);
#else
  int status = 0;
  waitpid(child.pid, &status, 0);
  child.pid = -1;
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
#endif
}

inline void terminate_process(ChildProcess& child) {
  if (!child.valid()) {
    return;
  }
#ifdef _WIN32
  TerminateProcess(child.info.hProcess, 1);
  wait_process(child);
#else
  kill(child.pid, SIGTERM);
  wait_process(child);
#endif
}

}  // namespace bctest

#endif  // BLOCKCHAIN_TESTS_PROCESS_SPAWN_HPP
