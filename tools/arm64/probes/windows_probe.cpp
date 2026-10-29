#include <windows.h>
#include <icu.h>
#include <cstdio>
#include <cwchar>

int main() {
#if !defined(_M_ARM64) || defined(_M_ARM64EC)
#error This probe must be compiled for native ARM64.
#endif
    if (u_toupper('a') != 'A') return 1;
    std::puts("PASS: Windows ICU C API linked and executed in a native ARM64 process");
    const wchar_t *libraries[] = {L"icu.dll", L"user32.dll", L"gdi32.dll", L"gdiplus.dll",
        L"shell32.dll", L"shlwapi.dll", L"shcore.dll", L"ole32.dll", L"oleaut32.dll", L"oleacc.dll",
        L"msftedit.dll", L"riched20.dll", L"mshtml.dll", L"ieframe.dll", L"mapi32.dll",
        L"powrprof.dll", L"ws2_32.dll", L"winmm.dll", L"wininet.dll", L"odbc32.dll",
        L"odbccp32.dll", L"crypt32.dll", L"bcrypt.dll"};
    int failures = 0;
    for (auto name : libraries) {
        HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) { std::wprintf(L"LOAD FAIL: %ls error=%lu\n", name, GetLastError()); ++failures; }
        else { std::wprintf(L"LOAD PASS: %ls\n", name); FreeLibrary(module); }
    }
    std::puts("Library loading checks availability only; clipboard, browser, mail-provider and UI behavior need application testing.");
    return failures ? 2 : 0;
}
