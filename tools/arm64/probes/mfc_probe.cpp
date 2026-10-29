#include <afxwin.h>
#include <atlbase.h>
#include <cstdio>

#if !defined(_M_ARM64) || defined(_M_ARM64EC)
#error This probe must be compiled for native ARM64.
#endif

CWinApp application;

int main() {
    if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0)) return 1;

    CString label(L"Ditto");
    label.Append(L" ARM64");
    if (label != L"Ditto ARM64") return 2;

    const BYTE expected[] = {1, 2, 3, 4};
    BYTE actual[sizeof(expected)] = {};
    CMemFile memory;
    memory.Write(expected, sizeof(expected));
    memory.SeekToBegin();
    if (memory.Read(actual, sizeof(actual)) != sizeof(actual)) return 3;
    for (size_t index = 0; index < sizeof(expected); ++index) {
        if (actual[index] != expected[index]) return 4;
    }

    CComBSTR value(L"ARM64");
    if (value.Length() != 5) return 5;

    std::puts("PASS: native ARM64 MFC initialization, CString, CMemFile and ATL CComBSTR");
    return 0;
}
