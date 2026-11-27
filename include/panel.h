#pragma once

// What the web UI can see and do on the device (implemented in console_ui.cpp).
struct PanelStatus {
    const char* status;  // READY, FILLING, ON TARGET, ...
    bool filling;
    int weight;          // tenths of a grain
    int target;          // tenths of a grain
};

PanelStatus panelStatus();
bool panelFill();
void panelStop();
void panelTare();
