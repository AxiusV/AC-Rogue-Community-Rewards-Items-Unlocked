#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <mutex>

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr const char* ScriptName = "Community Rewards Items Unlocked";
constexpr const char* ScriptVersion = "v1.0";

// ============================================================================
// GLOBAL STATE - UNLOCKS
// ============================================================================

int g_unlockRecordsPatched = 0;
std::thread* g_scanThread = nullptr;
std::mutex g_unlockMutex;
volatile bool g_scannerRunning = false;

// ============================================================================
// LOGGING UTILITIES
// ============================================================================

void Log(const char* text) {
    printf("[ITEMS_UNLOCKED] %s\n", text);
    OutputDebugStringA("[ITEMS_UNLOCKED] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

void Logf(const char* format, ...) {
    char buffer[1024]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    Log(buffer);
}

void LogWarning(const char* text) {
    printf("[ITEMS_UNLOCKED WARNING] %s\n", text);
    OutputDebugStringA("[ITEMS_UNLOCKED WARNING] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

void LogError(const char* text) {
    printf("[ITEMS_UNLOCKED ERROR] %s\n", text);
    OutputDebugStringA("[ITEMS_UNLOCKED ERROR] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

// ============================================================================
// MEMORY UTILITIES
// ============================================================================

bool TryWriteByte(std::uintptr_t address, std::uint8_t value) {
    if (!address) return false;
    __try {
        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(address), 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }
        *reinterpret_cast<std::uint8_t*>(address) = value;
        DWORD dummy = 0;
        VirtualProtect(reinterpret_cast<void*>(address), 1, oldProtect, &dummy);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsReadableRegion(const MEMORY_BASIC_INFORMATION& mbi) {
    if ((mbi.Protect & PAGE_NOACCESS) != 0 || (mbi.Protect & PAGE_GUARD) != 0) {
        return false;
    }
    const DWORD readableMask = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
    if ((mbi.Protect & readableMask) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    return true;
}

// ============================================================================
// PATTERN SEARCH
// ============================================================================

std::uintptr_t FindProcessBytes(const std::uint8_t* pattern, std::size_t patternSize, 
                                 std::size_t alignment) {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const std::uintptr_t maxAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

    while (cursor < maxAddress) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
            cursor += 0x1000;
            continue;
        }

        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t next = base + mbi.RegionSize;

        if (IsReadableRegion(mbi) && mbi.RegionSize >= patternSize) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(base);
            
            __try {
                for (std::size_t offset = 0; offset <= mbi.RegionSize - patternSize; ++offset) {
                    const std::uintptr_t current = base + offset;
                    if ((alignment == 0 || current % alignment == 0) && 
                        memcmp(bytes + offset, pattern, patternSize) == 0) {
                        return current;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        if (next <= cursor) break;
        cursor = next;
    }

    return 0;
}

// ============================================================================
// CORE UNLOCK LOGIC
// ============================================================================

bool ApplyLegacyContent() {
    constexpr char key[] = "ULCDRM000000";
    const auto address = FindProcessBytes(reinterpret_cast<const std::uint8_t*>(key), sizeof(key) - 1, 4);
    
    if (!address) {
        LogWarning("Legacy Content: marker pattern not found in game memory");
        return false;
    }

    Logf("Legacy Content: Found marker at 0x%p", reinterpret_cast<void*>(address));

    int writes = 0;
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const std::uintptr_t maxAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

    while (cursor < maxAddress) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
            cursor += 0x1000;
            continue;
        }

        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t next = base + mbi.RegionSize;

        if (IsReadableRegion(mbi) && mbi.RegionSize >= sizeof(key) - 1) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(base);
            
            __try {
                for (std::size_t offset = 0; offset <= mbi.RegionSize - (sizeof(key) - 1); ++offset) {
                    const std::uintptr_t current = base + offset;
                    if (current % 4 == 0 && memcmp(bytes + offset, key, sizeof(key) - 1) == 0) {
                        if (TryWriteByte(current + 0x11, 1)) {
                            ++writes;
                            Logf("Legacy Content: Patched at 0x%p (offset +0x11)", reinterpret_cast<void*>(current));
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        if (next <= cursor) break;
        cursor = next;
    }

    if (writes > 0) {
        Logf("Legacy Content: Successfully patched %d location(s)", writes);
        return true;
    } else {
        LogWarning("Legacy Content: Pattern found but no patches applied");
        return false;
    }
}

bool ApplyPlatformRewardPack() {
    constexpr char key[] = "ULCDRM000000";
    const auto address = FindProcessBytes(reinterpret_cast<const std::uint8_t*>(key), sizeof(key) - 1, 4);
    
    if (!address) {
        LogWarning("Platform Reward Pack: marker pattern not found in game memory");
        return false;
    }

    Logf("Platform Reward Pack: Found marker at 0x%p", reinterpret_cast<void*>(address));

    int writes = 0;
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const std::uintptr_t maxAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

    while (cursor < maxAddress) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
            cursor += 0x1000;
            continue;
        }

        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t next = base + mbi.RegionSize;

        if (IsReadableRegion(mbi) && mbi.RegionSize >= sizeof(key) - 1) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(base);
            
            __try {
                for (std::size_t offset = 0; offset <= mbi.RegionSize - (sizeof(key) - 1); ++offset) {
                    const std::uintptr_t current = base + offset;
                    if (current % 4 == 0 && memcmp(bytes + offset, key, sizeof(key) - 1) == 0) {
                        if (TryWriteByte(current + 0x11, 1)) {
                            ++writes;
                            Logf("Platform Reward Pack: Patched at 0x%p (offset +0x11)", reinterpret_cast<void*>(current));
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        if (next <= cursor) break;
        cursor = next;
    }

    if (writes > 0) {
        Logf("Platform Reward Pack: Successfully patched %d location(s)", writes);
        return true;
    } else {
        LogWarning("Platform Reward Pack: Pattern found but no patches applied");
        return false;
    }
}

void ScannerThreadProc() {
    Log("Background scanner thread started");
    
    while (g_scannerRunning) {
        Sleep(5000);
        
        // Apply both legacy content and platform reward pack unlocks
        bool legacySuccess = ApplyLegacyContent();
        bool platformSuccess = ApplyPlatformRewardPack();
        
        if (legacySuccess || platformSuccess) {
            g_unlockRecordsPatched++;
            Log("Unlocks completed. Waiting for next scan...");
        }
    }
    
    Log("Background scanner thread stopped");
}

// ============================================================================
// DLL EXPORTS
// ============================================================================

extern "C" {
    __declspec(dllexport) void InitializeMod() {
        if (!g_scanThread) {
            g_scannerRunning = true;
            g_scanThread = new std::thread(ScannerThreadProc);
            g_scanThread->detach();
            Log("ITEMS_UNLOCKED - Unlocks initialized - Legacy Content & Platform Rewards");
        }
    }

    __declspec(dllexport) void ShutdownMod() {
        g_scannerRunning = false;
        Sleep(100);
        Log("ITEMS_UNLOCKED - Unlocks shutdown");
    }

    __declspec(dllexport) bool UnlockLegacyContentNow() {
        return ApplyLegacyContent();
    }

    __declspec(dllexport) bool UnlockPlatformRewardPackNow() {
        return ApplyPlatformRewardPack();
    }

    __declspec(dllexport) int GetUnlockStatus() {
        return g_unlockRecordsPatched;
    }

    __declspec(dllexport) void InitializeASI() {
        InitializeMod();
    }

    __declspec(dllexport) void ShutdownASI() {
        ShutdownMod();
    }
}

// ============================================================================
// DLL MAIN ENTRY POINT
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            AllocConsole();
            
            // Hide the console window but keep the console functionality
            HWND consoleWindow = GetConsoleWindow();
            if (consoleWindow != NULL) {
                ShowWindow(consoleWindow, SW_HIDE);
            }
            
            FILE* fileout;
            freopen_s(&fileout, "CONOUT$", "w", stdout);
            
            Log("=================================================");
            Log("ITEMS_UNLOCKED - Legacy Content & Platform Rewards");
            Log("DLL Process Attached");
            Log("=================================================");
            
            InitializeMod();
            break;
        }
        case DLL_PROCESS_DETACH: {
            ShutdownMod();
            Log("ITEMS_UNLOCKED - Legacy Content & Platform Rewards - Process Detached");
            Log("=================================================");
            break;
        }
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}