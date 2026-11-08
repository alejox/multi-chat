#pragma once

// First-run assistant: welcome, first chat, the overlay sources in the current scene, and tips.

class QWidget;

// True when nothing is configured yet and the assistant was never shown (or skipped).
bool first_run_wizard_wanted();
// Opens it (or raises it if it is already open). Closing it in any way marks it as shown.
void open_first_run_wizard(QWidget *parent);
