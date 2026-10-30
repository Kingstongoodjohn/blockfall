// Blockfall - HTTPS requests and opening web pages on Windows (see http_win32.h)
#include "http_win32.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <vector>

namespace bf {

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
}  // namespace

HttpResponse winHttpRequest(const HttpRequest& req) {
    HttpResponse out;
    std::wstring url = widen(req.url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { out.error = "bad url"; return out; }
    std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) fullPath += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;

    Handle session(WinHttpOpen(L"Blockfall/15", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) session.h = WinHttpOpen(L"Blockfall/15", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { out.error = "no session"; return out; }
    WinHttpSetTimeouts(session.h, 8000, 8000, 10000, 15000);
    Handle conn(WinHttpConnect(session.h, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0));
    if (!conn.h) { out.error = "can't connect"; return out; }
    std::wstring verb = widen(req.method.empty() ? "GET" : req.method);
    Handle rq(WinHttpOpenRequest(conn.h, verb.c_str(), fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 https ? WINHTTP_FLAG_SECURE : 0));
    if (!rq.h) { out.error = "can't open request"; return out; }
    const wchar_t* headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    BOOL sent = WinHttpSendRequest(rq.h, headers, (DWORD)-1L, req.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)req.body.data(),
                                   (DWORD)req.body.size(), (DWORD)req.body.size(), 0);
    if (!sent || !WinHttpReceiveResponse(rq.h, nullptr)) { out.error = "no answer"; return out; }
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(rq.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    out.status = (int)status;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(rq.h, &avail) || avail == 0) break;
        if (out.body.size() + avail > 1 << 20) break;   // the server's answers are tiny
        std::vector<char> buf(avail);
        DWORD got = 0;
        if (!WinHttpReadData(rq.h, buf.data(), avail, &got) || got == 0) break;
        out.body.append(buf.data(), got);
    }
    return out;
}

void openInBrowser(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return;   // only real web pages, never files or programs
    std::wstring w = widen(url);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace bf
