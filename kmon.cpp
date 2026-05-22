#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <csignal>

#define C_RESET  "\033[0m"
#define C_BOLD   "\033[1m"
#define C_DIM    "\033[2m"
#define C_RED    "\033[91m"
#define C_GREEN  "\033[92m"
#define C_YELLOW "\033[93m"
#define C_CYAN   "\033[96m"
#define C_GRAY   "\033[90m"
#define C_WHITE  "\033[97m"

static volatile bool g_running  = true;
static int           g_interval = 500;
static FILE*         g_log      = nullptr;
static long long     g_loaded   = 0;
static long long     g_unloaded = 0;

static void onSignal(int) { g_running = false; }

static void enableAnsi() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static const char* getTime() {
    static char buf[16];
    SYSTEMTIME s;
    GetLocalTime(&s);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
             s.wHour, s.wMinute, s.wSecond, s.wMilliseconds);
    return buf;
}

struct Driver {
    LPVOID base;
    char   name[MAX_PATH];
    char   path[MAX_PATH];
};

// Returns number of drivers written into out[], max = capacity
static int snapshot(Driver* out, int capacity) {
    LPVOID addrs[4096];
    DWORD  needed = 0;

    if (!EnumDeviceDrivers(addrs, sizeof(addrs), &needed))
        return 0;

    int count = (int)(needed / sizeof(LPVOID));
    if (count > capacity) count = capacity;

    int filled = 0;
    for (int i = 0; i < count; i++) {
        if (!addrs[i]) continue;
        Driver d;
        d.base = addrs[i];
        d.name[0] = '\0';
        d.path[0] = '\0';
        GetDeviceDriverBaseNameA(addrs[i], d.name, MAX_PATH);
        GetDeviceDriverFileNameA(addrs[i], d.path, MAX_PATH);
        out[filled++] = d;
    }
    return filled;
}

static void printDriver(bool load, const Driver& d) {
    const char* tag   = load ? C_GREEN "LOAD  " C_RESET : C_RED "UNLOAD" C_RESET;
    const char* ptag  = load ? "LOAD  " : "UNLOAD";

    printf("  %s%s%s  %s  " C_WHITE C_BOLD "%-36s" C_RESET
           "  " C_CYAN "%p" C_RESET
           "  " C_DIM "%s" C_RESET "\n",
           C_GRAY, getTime(), C_RESET,
           tag,
           d.name[0] ? d.name : "(unknown)",
           d.base,
           d.path[0] ? d.path : "");

    if (g_log)
        fprintf(g_log, "  %s  %-6s  %-36s  %p  %s\n",
                getTime(), ptag,
                d.name[0] ? d.name : "(unknown)",
                d.base,
                d.path[0] ? d.path : "");

    load ? ++g_loaded : ++g_unloaded;
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    enableAnsi();
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    bool showAll = false;
    bool doLog   = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--all"))
            showAll = true;
        else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--log"))
            doLog = true;
        else if ((!strcmp(argv[i], "-i") || !strcmp(argv[i], "--interval")) && i+1 < argc)
            g_interval = std::max(50, std::min(5000, atoi(argv[++i])));
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            puts("kmon - kernel driver monitor\n"
                 "\n"
                 "  -a          show all loaded drivers on start\n"
                 "  -l          write events to kmon.log\n"
                 "  -i <ms>     poll interval (default: 500ms)\n"
                 "\n"
                 "  run as administrator for full visibility");
            return 0;
        }
    }

    // elevation check
    bool elevated = false;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te = {};
        DWORD len = 0;
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &len))
            elevated = te.TokenIsElevated != 0;
        CloseHandle(tok);
    }

    if (doLog) {
        g_log = fopen("kmon.log", "a");
        if (g_log)
            fprintf(g_log, "\n=== session started %s ===\n", getTime());
    }

    // initial snapshot
    static Driver buf1[4096];
    static Driver buf2[4096];

    int prevCount = snapshot(buf1, 4096);
    if (prevCount == 0) {
        fputs("error: failed to enumerate drivers\n", stderr);
        return 1;
    }

    // build prev map: base -> index
    std::unordered_map<uintptr_t, int> prevMap;
    for (int i = 0; i < prevCount; i++)
        prevMap[(uintptr_t)buf1[i].base] = i;

    // header
    printf("\n  " C_BOLD "kmon" C_RESET
           C_GRAY " - kernel driver monitor" C_RESET
           "  " C_DIM "[%d drivers  %dms  %s%s]\n\n" C_RESET,
           prevCount, g_interval,
           doLog ? "logging to kmon.log" : "no log",
           !elevated ? "  " C_YELLOW "not elevated" C_RESET C_DIM : "");

    if (!elevated)
        printf("  " C_YELLOW "warning:" C_RESET
               " run as administrator for complete visibility\n\n");

    printf(C_GRAY "  %-12s  %-6s  %-36s  %-18s  %s\n"
                  "  %.*s\n\n" C_RESET,
           "time", "event", "driver", "base", "path",
           100, "----------------------------------------------------------------------------------------------------");

    if (showAll) {
        for (int i = 0; i < prevCount; i++)
            printDriver(true, buf1[i]);
        printf(C_GRAY "\n  ---\n\n" C_RESET);
        g_loaded = 0; // reset counter, those aren't real events
    }

    // monitor loop
    while (g_running) {
        Sleep(g_interval);
        if (!g_running) break;

        int currCount = snapshot(buf2, 4096);
        std::unordered_map<uintptr_t, int> currMap;
        for (int i = 0; i < currCount; i++)
            currMap[(uintptr_t)buf2[i].base] = i;

        // newly loaded
        for (int i = 0; i < currCount; i++)
            if (prevMap.find((uintptr_t)buf2[i].base) == prevMap.end())
                printDriver(true, buf2[i]);

        // unloaded
        for (int i = 0; i < prevCount; i++)
            if (currMap.find((uintptr_t)buf1[i].base) == currMap.end())
                printDriver(false, buf1[i]);

        // swap buffers
        memcpy(buf1, buf2, currCount * sizeof(Driver));
        prevCount = currCount;
        prevMap   = std::move(currMap);
    }

    printf(C_GRAY "\n  loaded: " C_GREEN "%lld"
                  C_GRAY "  unloaded: " C_RED "%lld\n\n" C_RESET,
           g_loaded, g_unloaded);

    if (g_log) {
        fprintf(g_log, "=== session ended %s (loaded:%lld unloaded:%lld) ===\n",
                getTime(), g_loaded, g_unloaded);
        fclose(g_log);
    }
    return 0;
}
