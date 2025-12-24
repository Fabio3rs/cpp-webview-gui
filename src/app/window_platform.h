#pragma once
// =============================================================================
// Window platform helpers - keep native code in .cpp
// =============================================================================

namespace app {

void attach_window_to_parent(void *parent_window, void *child_window);
void move_window_to(void *window, int left, int top);

} // namespace app
