/*
 * Cross-platform Blue.Process and Blue.System implementations (Linux + Windows).
 */

#include "jsc_blue_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "jsc_runtime.h"

jsc_hybrid_aot_dispatch_fn g_aot_dispatch = nullptr;

extern "C" void jsc_hybrid_set_aot_dispatch(jsc_hybrid_aot_dispatch_fn fn) {
    g_aot_dispatch = fn;
}

extern "C" char* jsc_hybrid_call_aot_cstr(const char* function_name,
                                          const char* arg_utf8) {
    if (!g_aot_dispatch || !function_name || !function_name[0])
        return nullptr;
    JsValue out = g_aot_dispatch(function_name, arg_utf8 ? arg_utf8 : "");
    char* s = js_to_cstr(out);
    js_dispose_value(&out);
    if (!s)
        return nullptr;
    char* dup = strdup(s);
    free(s);
    return dup;
}

extern "C" BlueExecResult* jsc_blue_process_exec(const char* command_utf8) {
    BlueExecResult* r = (BlueExecResult*)calloc(1, sizeof(BlueExecResult));
    if (!r)
        return nullptr;
    r->stdout_txt = strdup("");
    r->stderr_txt = strdup("");
    if (!command_utf8 || !command_utf8[0]) {
        r->exit_code = -1;
        return r;
    }
#if defined(__linux__) || defined(__APPLE__)
    {
        FILE* po = popen(command_utf8, "r");
        if (!po) {
            r->exit_code = -1;
            free(r->stderr_txt);
            r->stderr_txt = strdup("popen failed");
            return r;
        }
        char           buf[4096];
        size_t         total = 0;
        unsigned char* acc = nullptr;
        while (fgets(buf, sizeof buf, po)) {
            size_t         n = strlen(buf);
            unsigned char* nacc =
                (unsigned char*)realloc(acc, total + n + 1);
            if (!nacc) {
                free(acc);
                pclose(po);
                r->exit_code = -1;
                return r;
            }
            acc = nacc;
            memcpy(acc + total, buf, n);
            total += n;
            acc[total] = '\0';
        }
        int st = pclose(po);
        if (WIFEXITED(st))
            r->exit_code = WEXITSTATUS(st);
        else if (WIFSIGNALED(st))
            r->exit_code = 128 + WTERMSIG(st);
        else
            r->exit_code = -1;
        free(r->stdout_txt);
        r->stdout_txt = acc ? (char*)acc : strdup("");
    }
#elif defined(_WIN32)
    {
        /* Convert UTF-8 command to wide; run via cmd.exe /C to match popen semantics. */
        int wlen = MultiByteToWideChar(CP_UTF8, 0, command_utf8, -1, nullptr, 0);
        if (wlen <= 0) { r->exit_code = -1; return r; }
        WCHAR* wcmd_raw = (WCHAR*)malloc((size_t)wlen * sizeof(WCHAR));
        if (!wcmd_raw) { r->exit_code = -1; return r; }
        MultiByteToWideChar(CP_UTF8, 0, command_utf8, -1, wcmd_raw, wlen);

        /* Prefix with "cmd.exe /C " */
        static const WCHAR prefix[] = L"cmd.exe /C ";
        size_t prefix_len = sizeof(prefix)/sizeof(WCHAR) - 1;
        size_t full_len = prefix_len + (size_t)wlen + 1;
        if (full_len < (size_t)wlen) { free(wcmd_raw); r->exit_code = -1; return r; } /* overflow */
        WCHAR* full_cmd = (WCHAR*)malloc(full_len * sizeof(WCHAR));
        if (!full_cmd) { free(wcmd_raw); r->exit_code = -1; return r; }
        memcpy(full_cmd, prefix, prefix_len * sizeof(WCHAR));
        memcpy(full_cmd + prefix_len, wcmd_raw, wlen * sizeof(WCHAR));
        free(wcmd_raw);

        /* Create pipe for stdout+stderr capture. */
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hRead = NULL, hWrite = NULL;
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
            free(full_cmd);
            r->exit_code = -1;
            return r;
        }
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si = {};
        si.cb          = sizeof(si);
        si.hStdOutput  = hWrite;
        si.hStdError   = hWrite;
        si.dwFlags     = STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessW(nullptr, full_cmd, nullptr, nullptr,
                                 TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        free(full_cmd);
        CloseHandle(hWrite);

        if (!ok) {
            CloseHandle(hRead);
            r->exit_code = -1;
            return r;
        }

        char           buf[4096];
        size_t         total = 0;
        unsigned char* acc = nullptr;
        DWORD          nread = 0;
        while (ReadFile(hRead, buf, sizeof(buf), &nread, nullptr) && nread > 0) {
            unsigned char* nacc = (unsigned char*)realloc(acc, total + nread + 1);
            if (!nacc) { free(acc); acc = nullptr; break; }
            acc = nacc;
            memcpy(acc + total, buf, nread);
            total += nread;
            acc[total] = '\0';
        }
        CloseHandle(hRead);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        r->exit_code = (int)exit_code;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        free(r->stdout_txt);
        r->stdout_txt = acc ? (char*)acc : strdup("");
    }
#endif
    return r;
}

extern "C" void jsc_blue_exec_result_free(BlueExecResult* r) {
    if (!r)
        return;
    free(r->stdout_txt);
    free(r->stderr_txt);
    free(r);
}

extern "C" char* jsc_blue_system_memory_json(void) {
#ifdef __linux__
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f)
        return strdup("{\"supported\":false}");
    unsigned long mt = 0, ma = 0, mf = 0;
    char          line[256];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemTotal: %lu kB", &mt) == 1) continue;
        if (sscanf(line, "MemAvailable: %lu kB", &ma) == 1) continue;
        if (sscanf(line, "MemFree: %lu kB", &mf) == 1) continue;
    }
    fclose(f);
    char buf[256];
    snprintf(buf, sizeof buf,
             "{\"supported\":true,\"memTotalKb\":%lu,"
             "\"memAvailableKb\":%lu,\"memFreeKb\":%lu}",
             mt, ma ? ma : mf, mf);
    return strdup(buf);
#elif defined(_WIN32)
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return strdup("{\"supported\":false}");
    char buf[256];
    snprintf(buf, sizeof buf,
             "{\"supported\":true,\"memTotalKb\":%llu,"
             "\"memAvailableKb\":%llu,\"memFreeKb\":%llu}",
             (unsigned long long)(ms.ullTotalPhys / 1024),
             (unsigned long long)(ms.ullAvailPhys / 1024),
             (unsigned long long)(ms.ullAvailPhys / 1024));
    return strdup(buf);
#else
    return strdup("{\"supported\":false}");
#endif
}

extern "C" char* jsc_blue_system_cpu_json(void) {
#ifdef __linux__
    double la1 = 0, la5 = 0, la15 = 0;
    FILE*  f = fopen("/proc/loadavg", "r");
    if (!f || fscanf(f, "%lf %lf %lf", &la1, &la5, &la15) < 3) {
        if (f) fclose(f);
        return strdup("{\"supported\":false}");
    }
    fclose(f);
    char buf[160];
    snprintf(buf, sizeof buf,
             "{\"supported\":true,\"loadAvg\":[%.3f,%.3f,%.3f]}", la1, la5,
             la15);
    return strdup(buf);
#elif defined(_WIN32)
    /* GetSystemTimes returns cumulative 100ns ticks. kernel includes idle. */
    FILETIME idle_ft, kernel_ft, user_ft;
    if (!GetSystemTimes(&idle_ft, &kernel_ft, &user_ft))
        return strdup("{\"supported\":false}");
    auto ft64 = [](FILETIME ft) -> unsigned long long {
        return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    };
    unsigned long long idle   = ft64(idle_ft);
    unsigned long long kernel = ft64(kernel_ft); /* includes idle */
    unsigned long long user   = ft64(user_ft);
    unsigned long long total  = kernel + user;
    unsigned long long busy   = total > idle ? total - idle : 0;
    double pct = total > 0 ? (double)busy / (double)total * 100.0 : 0.0;
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    char buf[160];
    snprintf(buf, sizeof buf,
             "{\"supported\":true,\"loadAvg\":[%.2f,%.2f,%.2f],\"cpuCount\":%lu}",
             pct, pct, pct, (unsigned long)si.dwNumberOfProcessors);
    return strdup(buf);
#else
    return strdup("{\"supported\":false}");
#endif
}


