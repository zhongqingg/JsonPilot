#pragma once
#include <string>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <filesystem>

constexpr int JSONEDITOR_PORT = 9391;

inline std::string urlEncode(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            result += c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            result += buf;
        }
    }
    return result;
}

// Application data directory for logs: %LOCALAPPDATA%\JsonPilot.
// Created on demand. Returns empty string if it cannot be resolved so
// callers can silently skip file logging instead of polluting the CWD.
inline const std::string& getAppDataDir() {
#ifdef _WIN32
    static const std::string dir = []() -> std::string {
        std::string base;
        if (const char* la = std::getenv("LOCALAPPDATA"); la && *la)
            base = la;
        else if (const char* up = std::getenv("USERPROFILE"); up && *up)
            base = std::string(up) + "\\AppData\\Local";
        if (base.empty()) return {};
        std::string full = base + "\\JsonPilot";
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(full), ec);
        return full;
    }();
    return dir;
#else
    static const std::string dir;
    return dir;
#endif
}

// Format a "[HH:MM:SS] msg\n" log line.
inline std::string logLine(const char* msg) {
    std::time_t t = std::time(nullptr);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return std::string("[") + buf + "] " + msg + "\n";
}

// Append a line to <AppData>\JsonPilot\<fileName>, capping the file at ~512 KB.
inline void writeAppLog(const char* fileName, const std::string& line) {
    const std::string& dir = getAppDataDir();
    if (dir.empty()) return;
    std::string path = dir + "\\" + fileName;
    std::error_code ec;
    auto size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    if (!ec && size > 512 * 1024)
        std::filesystem::remove(std::filesystem::u8path(path), ec);
    std::ofstream log(std::filesystem::u8path(path), std::ios::app);
    if (log.is_open()) { log << line; log.close(); }
}
