// Blockfall - HTTPS requests and opening web pages on Windows (WinHTTP, ShellExecute).
// Used by the K-Bucks store (src/game/store.h).
#pragma once
#include "../game/store.h"
#include <string>

namespace bf {

// Blocking HTTP(S) request with JSON body; runs on the store's worker thread.
HttpResponse winHttpRequest(const HttpRequest& req);
// Opens an https:// page in the default browser. Anything else is ignored.
void openInBrowser(const std::string& url);

}  // namespace bf
