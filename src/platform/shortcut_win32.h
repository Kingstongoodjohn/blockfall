// Blockfall - a "Blockfall" shortcut on the desktop that starts the game (with its icon).
#pragma once

namespace bf {

// Makes (or refreshes) Desktop\Blockfall.lnk pointing at this exe. Returns true on success.
bool createDesktopShortcut();

}  // namespace bf
