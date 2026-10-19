// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors
#include "leash/cli/doctor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace leash::cli {
namespace {

constexpr int kTimeoutMs = 5000;
constexpr int kPollStepMs = 100;

struct CommandResult {
    bool found{false};
    std::string detail;
};

[[nodiscard]] std::string first_line(std::string_view text) {
    const std::size_t end = text.find_first_of("\r\n");
    std::string line(text.substr(0, end));
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    return line;
}

[[nodiscard]] CommandResult run_version(const std::string& program) {
    std::array<int, 2> fds{};
    if (pipe(fds.data()) != 0) {
        return {.found = false, .detail = "pipe failed"};
    }
    const int read_fd = fds.at(0);
    const int write_fd = fds.at(1);

    const pid_t pid = fork();
    if (pid == 0) {
        static_cast<void>(close(read_fd));
        if (dup2(write_fd, STDOUT_FILENO) < 0 || dup2(write_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }
        static_cast<void>(close(write_fd));
        std::string path = program;
        std::array<char, 10> version_flag{"--version"};
        std::array<char*, 3> argv{path.data(), version_flag.data(), nullptr};
        execvp(path.c_str(), argv.data());
        _exit(127);
    }
    static_cast<void>(close(write_fd));
    if (pid < 0) {
        static_cast<void>(close(read_fd));
        return {.found = false, .detail = "fork failed"};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    bool timed_out = false;
    pollfd descriptor{.fd = read_fd, .events = POLLIN, .revents = 0};
    const int ready = poll(&descriptor, 1, kTimeoutMs);
    if (ready > 0 && (descriptor.revents & POLLIN) != 0) {
        const ssize_t count = read(read_fd, buffer.data(), buffer.size());
        if (count > 0) {
            output.assign(buffer.data(), static_cast<std::size_t>(count));
        }
    } else if (ready == 0) {
        timed_out = true;
    }

    int status = 0;
    bool reaped = false;
    for (int attempt = 0; attempt < kTimeoutMs / kPollStepMs; ++attempt) {
        if (waitpid(pid, &status, WNOHANG) == pid) {
            reaped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollStepMs));
    }
    if (!reaped) {
        static_cast<void>(kill(pid, SIGKILL));
        static_cast<void>(waitpid(pid, &status, 0));
        timed_out = true;
    }
    static_cast<void>(close(read_fd));

    const bool succeeded = !timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    const std::string line = first_line(output);
    if (succeeded) {
        return {.found = true, .detail = line};
    }
    std::string detail;
    if (!line.empty()) {
        detail = line;
    } else if (timed_out) {
        detail = "timed out";
    } else {
        detail = "not found on PATH";
    }
    return {.found = false, .detail = detail};
}

} // namespace

std::string DoctorReport::render() const {
    std::size_t width = 0;
    for (const Probe& probe : probes) {
        width = std::max(width, probe.name.size());
    }
    std::string text;
    for (const Probe& probe : probes) {
        text += probe.name;
        text.append(width - probe.name.size(), ' ');
        text += "  ";
        text += probe.found ? "ok" : "missing";
        text += "  ";
        text += probe.detail;
        text += '\n';
    }
    return text;
}

DoctorReport collect_probes() {
    DoctorReport report;
    for (const std::string program : {"hermes", "claude", "codex"}) {
        const CommandResult result = run_version(program);
        report.probes.push_back(
            Probe{.name = program, .found = result.found, .detail = result.detail});
    }

    const char* api_key = std::getenv("ANTHROPIC_API_KEY");
    const bool key_set = api_key != nullptr && api_key[0] != '\0';
    report.probes.push_back(
        Probe{.name = "ANTHROPIC_API_KEY", .found = key_set, .detail = key_set ? "set" : "unset"});

    for (const std::string program : {"git", "cmake"}) {
        const CommandResult result = run_version(program);
        report.probes.push_back(
            Probe{.name = program, .found = result.found, .detail = result.detail});
    }
    return report;
}

} // namespace leash::cli
