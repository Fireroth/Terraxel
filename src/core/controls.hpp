#pragma once

#include <string>

struct ControlsConfig {
    int moveForward;
    int moveBackward;
    int moveLeft;
    int moveRight;
    int jumpUp;
    int crouchDown;
    int toggleFlyMode;
    int toggleWireframe;
    int sprint;
    int openInventory;
    int openConsole;
    int zoom;
    int toggleHotbar;
    int toggleDebug;
    int toggleFullscreen;
};


extern ControlsConfig g_controls;

void loadControlsFromFile(const std::string& filename);
void saveControlsToFile(const std::string& filename);
void initializeDefaultControls();
std::string getKeyName(int keyCode);
