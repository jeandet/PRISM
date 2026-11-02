#pragma once

#include "proc_metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

// A made-up machine for platforms without /proc (the web, macOS, Windows): smooth curves, and a
// few processes that start and stop, so every part of the monitor has something to show. Pure
// functions of the time in seconds, so they are testable and the same on every run.

inline SystemSample simulated_system_sample(double t) {
    double cpu = 28.0 + 14.0 * std::sin(t / 7.0) + 9.0 * std::sin(t * 1.3) + 5.0 * std::sin(t * 3.7);
    double mem = 6200.0 + 900.0 * std::sin(t / 23.0) + 150.0 * std::sin(t * 0.9);
    double rx = 180.0 + 160.0 * std::max(0.0, std::sin(t / 4.0)) + 40.0 * std::sin(t * 2.1);
    return {.cpu_percent = static_cast<float>(std::clamp(cpu, 0.0, 100.0)),
            .mem_used_mb = mem,
            .mem_total_mb = 16384.0,
            .net_rx_kbps = std::max(0.0, rx),
            .net_tx_kbps = std::max(0.0, rx * 0.3)};
}

inline std::vector<ProcessInfo> simulated_processes(double t) {
    struct Proc { int pid, ppid; const char* name; double cpu, phase; };
    static constexpr std::array<Proc, 10> always{{
        {1, 0, "init", 0.2, 0.0},          {120, 1, "sshd", 0.3, 1.0},
        {200, 1, "dbus-daemon", 0.5, 2.0}, {310, 1, "compositor", 6.0, 0.5},
        {420, 310, "prism-gallery", 4.0, 1.7}, {430, 310, "terminal", 1.5, 2.9},
        {431, 430, "zsh", 0.4, 0.3},       {520, 1, "sensor-daemon", 2.5, 4.1},
        {610, 1, "backup", 9.0, 3.3},      {700, 431, "make", 18.0, 0.9},
    }};
    std::vector<ProcessInfo> out;
    auto add = [&](int pid, int ppid, std::string name, double cpu, double phase) {
        double load = std::max(0.0, cpu * (0.6 + 0.4 * std::sin(t * 0.8 + phase)));
        out.push_back({.pid = pid, .ppid = ppid, .name = std::move(name), .state = 'S',
                       .cpu_percent = static_cast<float>(load),
                       .mem_percent = static_cast<float>(0.4 + 0.1 * pid / 70.0)});
    };
    for (const auto& p : always) add(p.pid, p.ppid, p.name, p.cpu, p.phase);
    // Compiler jobs under make: each lives 6 s, a new one every 3 s, so starts and stops show.
    for (int job = 0; job < 3; ++job) {
        int born = static_cast<int>(t / 3.0) - job;
        if (born < 0) continue;
        add(800 + born, 700, "cc1plus #" + std::to_string(born), 35.0, born);
    }
    return out;
}

// Whether this machine has the Linux /proc files the real sampler reads.
inline bool has_proc() {
    std::error_code ec;
    return std::filesystem::exists("/proc/stat", ec) && std::filesystem::exists("/proc/meminfo", ec);
}
