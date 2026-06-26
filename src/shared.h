#pragma once
#include <string>
#include <cstdio>
#include <cctype>

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
