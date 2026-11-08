#pragma once

// First-run assistant: welcome, first channel (platform preselected from the streaming service),
// the chat, featured and voice sources in the current scene with one click, and tips. Finishing
// shows the dock.

class QWidget;

// True when nothing is configured yet and the assistant was never shown (or skipped).
bool first_run_wizard_wanted();
// Opens it (or raises it if it is already open). Closing it in any way marks it as shown.
void open_first_run_wizard(QWidget *parent);
