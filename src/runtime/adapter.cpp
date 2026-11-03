#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>

#include "config.h"
#include "parsed.h"

namespace mfdp {

namespace {

constexpr size_t kMaxAdapterOutput = 256ull * 1024 * 1024;
constexpr int kDefaultTimeoutSeconds = 60;
constexpr int kBuildTimeoutSeconds = 900;
constexpr int kProbeTimeoutSeconds = 30;
constexpr int kMinJavaMajor = 11;

struct ProcessResult {
  std::string out;
  std::string err;
  int exitCode = -1;
  bool timedOut = false;
  bool outputTooLarge = false;
};

int makePipe(int fds[2]) {
#if defined(__linux__)
  return ::pipe2(fds, O_CLOEXEC);
#else
  if (::pipe(fds) != 0) return -1;
  ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  return 0;
#endif
}

void setNonBlocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

ProcessResult runProcess(const std::vector<std::string>& argv, const std::string& input, int timeoutSeconds) {
  struct sigaction ignore;
  std::memset(&ignore, 0, sizeof(ignore));
  ignore.sa_handler = SIG_IGN;
  ::sigaction(SIGPIPE, &ignore, nullptr);

  int inPipe[2];
  int outPipe[2];
  int errPipe[2];
  if (makePipe(inPipe) != 0) throw Error(ErrorCode::Io, "pipe failed");
  if (makePipe(outPipe) != 0) throw Error(ErrorCode::Io, "pipe failed");
  if (makePipe(errPipe) != 0) throw Error(ErrorCode::Io, "pipe failed");

  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0) throw Error(ErrorCode::Io, "fork failed");
  if (pid == 0) {
    ::dup2(inPipe[0], 0);
    ::dup2(outPipe[1], 1);
    ::dup2(errPipe[1], 2);
    ::execvp(args[0], args.data());
    ::_exit(127);
  }
  ::close(inPipe[0]);
  ::close(outPipe[1]);
  ::close(errPipe[1]);
  setNonBlocking(inPipe[1]);
  setNonBlocking(outPipe[0]);
  setNonBlocking(errPipe[0]);

  ProcessResult result;
  size_t written = 0;
  bool inOpen = true;
  bool outOpen = true;
  bool errOpen = true;
  if (input.empty()) {
    ::close(inPipe[1]);
    inOpen = false;
  }
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
  char buf[65536];

  while (inOpen || outOpen || errOpen) {
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      result.timedOut = true;
      ::kill(pid, SIGKILL);
      break;
    }
    int remainingMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    struct pollfd fds[3];
    int nfds = 0;
    int inIdx = -1, outIdx = -1, errIdx = -1;
    if (inOpen) {
      fds[nfds] = {inPipe[1], POLLOUT, 0};
      inIdx = nfds++;
    }
    if (outOpen) {
      fds[nfds] = {outPipe[0], POLLIN, 0};
      outIdx = nfds++;
    }
    if (errOpen) {
      fds[nfds] = {errPipe[0], POLLIN, 0};
      errIdx = nfds++;
    }
    int rc = ::poll(fds, nfds, remainingMs > 1000 ? 1000 : remainingMs);
    if (rc < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (inIdx >= 0 && (fds[inIdx].revents & (POLLOUT | POLLERR | POLLHUP))) {
      ssize_t w = ::write(inPipe[1], input.data() + written, input.size() - written);
      if (w > 0) {
        written += static_cast<size_t>(w);
      } else if (w < 0 && errno != EAGAIN && errno != EINTR) {
        written = input.size();
      }
      if (written >= input.size()) {
        ::close(inPipe[1]);
        inOpen = false;
      }
    }
    if (outIdx >= 0 && (fds[outIdx].revents & (POLLIN | POLLHUP | POLLERR))) {
      ssize_t r = ::read(outPipe[0], buf, sizeof(buf));
      if (r > 0) {
        result.out.append(buf, static_cast<size_t>(r));
        if (result.out.size() > kMaxAdapterOutput) {
          result.outputTooLarge = true;
          ::kill(pid, SIGKILL);
          break;
        }
      } else if (r == 0 || (errno != EAGAIN && errno != EINTR)) {
        ::close(outPipe[0]);
        outOpen = false;
      }
    }
    if (errIdx >= 0 && (fds[errIdx].revents & (POLLIN | POLLHUP | POLLERR))) {
      ssize_t r = ::read(errPipe[0], buf, sizeof(buf));
      if (r > 0) {
        if (result.err.size() < 65536) result.err.append(buf, static_cast<size_t>(r));
      } else if (r == 0 || (errno != EAGAIN && errno != EINTR)) {
        ::close(errPipe[0]);
        errOpen = false;
      }
    }
  }
  if (inOpen) ::close(inPipe[1]);
  if (outOpen) ::close(outPipe[0]);
  if (errOpen) ::close(errPipe[0]);
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
  else result.exitCode = -1;
  return result;
}

int parseJavaMajor(const std::string& text) {
  size_t pos = text.find("version \"");
  if (pos == std::string::npos) return -1;
  pos += 9;
  auto readInt = [&](size_t& i) {
    int v = -1;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      if (v < 0) v = 0;
      v = v * 10 + (text[i] - '0');
      ++i;
    }
    return v;
  };
  size_t i = pos;
  int first = readInt(i);
  if (first < 0) return -1;
  if (first == 1 && i < text.size() && text[i] == '.') {
    ++i;
    return readInt(i);
  }
  return first;
}

void requireJava(const std::string& javaCommand) {
  static std::mutex lock;
  static std::map<std::string, int> cache;
  int major = 0;
  {
    std::lock_guard<std::mutex> guard(lock);
    auto it = cache.find(javaCommand);
    if (it != cache.end()) {
      major = it->second;
    } else {
      ProcessResult r = runProcess({javaCommand, "-version"}, "", kProbeTimeoutSeconds);
      if (r.exitCode == 127) {
        major = -127;
      } else {
        major = parseJavaMajor(r.err + "\n" + r.out);
      }
      cache[javaCommand] = major;
    }
  }
  if (major == -127) {
    throw Error(ErrorCode::AdapterUnavailable,
                "could not execute '" + javaCommand +
                    "'; install a JDK " + std::to_string(kMinJavaMajor) + "+ or set MFDP_JAVA");
  }
  if (major >= 0 && major < kMinJavaMajor) {
    throw Error(ErrorCode::AdapterUnavailable,
                "the Java adapter needs a JDK " + std::to_string(kMinJavaMajor) + "+ but '" + javaCommand +
                    "' is version " + std::to_string(major) + "; install a newer JDK or set MFDP_JAVA");
  }
}

std::string lastJsonLine(const std::string& out) {
  if (!out.empty() && out[0] == '{') return out;
  size_t pos = out.size();
  while (pos > 0) {
    size_t nl = out.rfind('\n', pos - 1);
    size_t begin = nl == std::string::npos ? 0 : nl + 1;
    if (begin < out.size() && out[begin] == '{') return out.substr(begin);
    if (nl == std::string::npos) break;
    pos = nl;
  }
  return out;
}

}

std::string runAdapterProcess(const std::string& language, const std::string& filename,
                              const std::string& content) {
  AdapterSpec spec = resolveAdapter(language, filename);
  std::vector<std::string> argv;
  int timeout = kDefaultTimeoutSeconds;
  auto envOr = [](const char* name, const char* fallback) {
    return envValue(name, fallback);
  };
  const std::string& kind = spec.kind;
  if (kind == "python") {
    argv.push_back(envOr("MFDP_PYTHON", "python3"));
    argv.push_back(spec.path);
  } else if (kind == "node") {
    argv.push_back(envOr("MFDP_NODE", "node"));
    argv.push_back(spec.path);
  } else if (kind == "go") {
    argv.push_back(envOr("MFDP_GO", "go"));
    argv.push_back("run");
    argv.push_back(spec.path);
  } else if (kind == "java") {
    std::string javaCommand = envOr("MFDP_JAVA", "java");
    requireJava(javaCommand);
    argv.push_back(javaCommand);
    argv.push_back("-XX:TieredStopAtLevel=1");
    argv.push_back("-Xshare:auto");
    argv.push_back(spec.path);
  } else if (kind == "rust") {
    argv.push_back(envOr("MFDP_CARGO", "cargo"));
    argv.push_back("run");
    argv.push_back("--release");
    argv.push_back("--quiet");
    argv.push_back("--manifest-path");
    argv.push_back(spec.path + "/Cargo.toml");
    argv.push_back("--");
    timeout = kBuildTimeoutSeconds;
  } else if (kind == "csharp") {
    argv.push_back(envOr("MFDP_DOTNET", "dotnet"));
    argv.push_back("run");
    argv.push_back("--project");
    argv.push_back(spec.path);
    argv.push_back("-c");
    argv.push_back("Release");
    argv.push_back("--nologo");
    argv.push_back("-v");
    argv.push_back("q");
    argv.push_back("--");
    timeout = kBuildTimeoutSeconds;
    ::setenv("DOTNET_NOLOGO", "1", 0);
    ::setenv("DOTNET_CLI_TELEMETRY_OPTOUT", "1", 0);
    ::setenv("DOTNET_SKIP_FIRST_TIME_EXPERIENCE", "1", 0);
  } else {
    argv.push_back(spec.path);
  }
  const char* tenv = std::getenv("MFDP_ADAPTER_TIMEOUT");
  if (tenv && *tenv) {
    int t = std::atoi(tenv);
    if (t > 0) timeout = t;
  }
  argv.push_back("--language");
  argv.push_back(language);
  argv.push_back("--filename");
  argv.push_back(filename);

  ProcessResult r = runProcess(argv, content, timeout);
  if (r.timedOut) {
    throw Error(ErrorCode::AdapterFailed,
                "parser adapter timed out after " + std::to_string(timeout) +
                    "s (set MFDP_ADAPTER_TIMEOUT to allow more time; first runs compile the adapter)",
                filename);
  }
  if (r.outputTooLarge) {
    throw Error(ErrorCode::TooLarge, "parser adapter output exceeded the size limit", filename);
  }
  if (r.exitCode == 127) {
    throw Error(ErrorCode::AdapterUnavailable,
                "could not execute '" + argv[0] +
                    "'; install it or set MFDP_PYTHON, MFDP_NODE, MFDP_GO, MFDP_JAVA, MFDP_CARGO or MFDP_DOTNET");
  }
  if (r.exitCode != 0) {
    std::string detail = r.err.size() > 2000 ? r.err.substr(0, 2000) : r.err;
    throw Error(ErrorCode::AdapterFailed,
                "parser adapter exited with status " + std::to_string(r.exitCode) + ": " + detail,
                filename);
  }
  return lastJsonLine(r.out);
}

}