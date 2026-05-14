#pragma once

#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <climits>
#include <cstring>
#include <filesystem>
#include <string>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace fs = std::filesystem;

static std::string binDir(const char* argv0) {
#ifdef __linux__
    char buf[PATH_MAX + 1] = {};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return fs::absolute(buf).parent_path().generic_string();
    }
#endif
#ifdef _WIN32
    wchar_t wbuf[32768] = {};
    DWORD n = GetModuleFileNameW(NULL, wbuf, (DWORD)(sizeof(wbuf) / sizeof(wchar_t)) - 1);
    if (n > 0)
        return fs::path(wbuf).parent_path().generic_string();
#endif
    if (argv0 && argv0[0] == '/')
        return fs::path(argv0).parent_path().generic_string();
#ifdef __APPLE__
    char abuf[PATH_MAX + 1];
    uint32_t sz = sizeof(abuf);
    if (_NSGetExecutablePath(abuf, &sz) == 0)
        return fs::absolute(abuf).parent_path().generic_string();
#endif
    char tmp[PATH_MAX];
    std::strncpy(tmp, argv0 ? argv0 : "", sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    return fs::absolute(fs::current_path() / fs::path(tmp))
        .parent_path()
        .generic_string();
}
