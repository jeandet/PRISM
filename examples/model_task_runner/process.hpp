#pragma once

// The one place in this example that talks to the operating system: a child process with its
// stdout and stderr on a single non-blocking pipe, read by the caller's own thread, killed on
// request. Everything else in the app is portable.
//
// A browser has no process to start, so the web build gets the flag and nothing else.

#include <string>
#include <string_view>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#define PRISM_TASK_RUNNER_POSIX 1
#else
#define PRISM_TASK_RUNNER_POSIX 0
#endif

#if PRISM_TASK_RUNNER_POSIX
// The C headers stay at global scope: included inside the namespace, they would declare poll(),
// read(), waitpid() and friends in prism::runner, where the ::-qualified calls could not reach
// them (and posix_spawnp would get a private, never-defined `environ`).
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;   // what the child inherits
#endif

namespace prism::runner {

// What the app asks before offering a task: can this build start a process at all?
inline constexpr bool kNativeProcesses = PRISM_TASK_RUNNER_POSIX != 0;

#if PRISM_TASK_RUNNER_POSIX

// A child running `/bin/sh -c <command>`, its output read through one pipe. Owns both ends:
// dropping it stops the child.
class ChildProcess {
public:
    // Nothing usable comes back when the shell cannot start; `error` says why.
    static std::optional<ChildProcess> spawn_shell(std::string_view command, std::string& error);

    ChildProcess(ChildProcess&& other) noexcept
        : pid_(std::exchange(other.pid_, -1))
        , read_fd_(std::exchange(other.read_fd_, -1))
        , code_(std::exchange(other.code_, std::nullopt)) {}

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess& operator=(ChildProcess&&) = delete;

    ~ChildProcess() {
        if (!code_) stop(std::chrono::milliseconds{200});
    }

    // At least one byte, 0 at the end of the output, -1 when there is nothing to read yet.
    std::ptrdiff_t read_some(char* buffer, std::size_t size) {
        if (read_fd_ < 0) return 0;
        pollfd fd{.fd = read_fd_, .events = POLLIN, .revents = 0};
        const int ready = ::poll(&fd, 1, static_cast<int>(kPollSlice.count()));
        if (ready <= 0) return -1;
        const auto got = ::read(read_fd_, buffer, size);
        if (got > 0) return got;
        if (got < 0) return errno == EAGAIN || errno == EINTR ? -1 : 0;
        return 0;
    }

    // The child's exit code; 128 + N when a signal ended it.
    int wait() {
        if (code_) {
            close_pipe();
            return *code_;
        }
        int status = 0;
        while (pid_ > 0) {
            if (::waitpid(pid_, &status, 0) == pid_) {
                record(status);
                break;
            }
            if (errno != EINTR) break;
        }
        close_pipe();
        return code_.value_or(-1);
    }

    // Stop, politely then not: SIGTERM, a grace period, SIGKILL. Gives the exit code.
    int stop(std::chrono::milliseconds grace) {
        if (code_) return *code_;
        if (pid_ > 0) ::kill(pid_, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + grace;
        char scratch[4096];
        while (std::chrono::steady_clock::now() < deadline) {
            int status = 0;
            if (exited(status)) return wait();
            if (read_some(scratch, sizeof scratch) == 0) return wait();   // output over: it is done
        }
        if (pid_ > 0) ::kill(pid_, SIGKILL);
        return wait();
    }

private:
    // One poll slice: long enough not to spin, short enough to notice a stop request.
    static constexpr std::chrono::milliseconds kPollSlice{20};

    ChildProcess(pid_t pid, int read_fd) : pid_(pid), read_fd_(read_fd) {}

    bool exited(int& code) {
        if (code_) {
            code = *code_;
            return true;
        }
        if (pid_ <= 0) {
            code = -1;
            return true;
        }
        int status = 0;
        if (::waitpid(pid_, &status, WNOHANG) != pid_) return false;
        record(status);
        code = *code_;
        return true;
    }

    void record(int status) {
        code_ = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        pid_ = -1;
    }

    void close_pipe() {
        if (read_fd_ < 0) return;
        ::close(read_fd_);
        read_fd_ = -1;
    }

    pid_t pid_ = -1;
    int read_fd_ = -1;
    std::optional<int> code_{};   // the exit code, once the child has been reaped
};

inline std::optional<ChildProcess> ChildProcess::spawn_shell(std::string_view command, std::string& error) {
    int fds[2];
    if (::pipe(fds) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return std::nullopt;
    }
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[0], F_SETFL, O_NONBLOCK);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    posix_spawn_file_actions_addclose(&actions, fds[0]);

    const std::string shell_script{command};
    char* argv[] = {const_cast<char*>("/bin/sh"), const_cast<char*>("-c"),
                    const_cast<char*>(shell_script.c_str()), nullptr};
    pid_t pid = -1;
    const int spawned = ::posix_spawnp(&pid, "/bin/sh", &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);                     // the child has its own copy of the write end

    if (spawned != 0) {
        error = std::string("cannot run /bin/sh: ") + std::strerror(spawned);
        ::close(fds[0]);
        return std::nullopt;
    }
    return ChildProcess{pid, fds[0]};
}

#endif // PRISM_TASK_RUNNER_POSIX

} // namespace prism::runner

// PRISM_TASK_RUNNER_POSIX is left defined on purpose: the app guards its own process code with
// #if, which a constexpr bool cannot do.
