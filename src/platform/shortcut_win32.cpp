// Blockfall - desktop shortcut (see shortcut_win32.h)
#include "shortcut_win32.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>

namespace bf {

bool createDesktopShortcut() {
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    std::wstring dir(exe);
    size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? L"." : dir.substr(0, slash);

    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) return false;
    std::wstring link = std::wstring(desktop) + L"\\Blockfall.lnk";
    CoTaskMemFree(desktop);

    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW* sl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(exe);
        sl->SetWorkingDirectory(dir.c_str());
        sl->SetIconLocation(exe, 0);   // the B icon built into the exe (res/blockfall.ico)
        sl->SetDescription(L"Play Blockfall");
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->Save(link.c_str(), TRUE));
            pf->Release();
        }
        sl->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

}  // namespace bf
