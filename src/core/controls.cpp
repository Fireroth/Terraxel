#include <GLFW/glfw3.h>
#include <fstream>
#include <sstream>
#include <map>
#include "../core/logger.hpp"
#include "controls.hpp"

ControlsConfig g_controls;
static bool loaded = false;

static std::map<int, std::string> keyNameMap = {

    {GLFW_KEY_A, "A"},
    {GLFW_KEY_B, "B"},
    {GLFW_KEY_C, "C"},
    {GLFW_KEY_D, "D"},
    {GLFW_KEY_E, "E"},
    {GLFW_KEY_F, "F"},
    {GLFW_KEY_G, "G"},
    {GLFW_KEY_H, "H"},
    {GLFW_KEY_I, "I"},
    {GLFW_KEY_J, "J"},
    {GLFW_KEY_K, "K"},
    {GLFW_KEY_L, "L"},
    {GLFW_KEY_M, "M"},
    {GLFW_KEY_N, "N"},
    {GLFW_KEY_O, "O"},
    {GLFW_KEY_P, "P"},
    {GLFW_KEY_Q, "Q"},
    {GLFW_KEY_R, "R"},
    {GLFW_KEY_S, "S"},
    {GLFW_KEY_T, "T"},
    {GLFW_KEY_U, "U"},
    {GLFW_KEY_V, "V"},
    {GLFW_KEY_W, "W"},
    {GLFW_KEY_X, "X"},
    {GLFW_KEY_Y, "Y"},
    {GLFW_KEY_Z, "Z"},

    {GLFW_KEY_0, "0"},
    {GLFW_KEY_1, "1"},
    {GLFW_KEY_2, "2"},
    {GLFW_KEY_3, "3"},
    {GLFW_KEY_4, "4"},
    {GLFW_KEY_5, "5"},
    {GLFW_KEY_6, "6"},
    {GLFW_KEY_7, "7"},
    {GLFW_KEY_8, "8"},
    {GLFW_KEY_9, "9"},

    {GLFW_KEY_SPACE, "SPACE"},
    {GLFW_KEY_APOSTROPHE, "'"},
    {GLFW_KEY_COMMA, ","},
    {GLFW_KEY_MINUS, "-"},
    {GLFW_KEY_PERIOD, "."},
    {GLFW_KEY_SLASH, "/"},
    {GLFW_KEY_SEMICOLON, ";"},
    {GLFW_KEY_EQUAL, "="},
    {GLFW_KEY_LEFT_BRACKET, "["},
    {GLFW_KEY_BACKSLASH, "\\"},
    {GLFW_KEY_RIGHT_BRACKET, "]"},
    {GLFW_KEY_GRAVE_ACCENT, "`"},

    {GLFW_KEY_F1, "F1"},
    {GLFW_KEY_F2, "F2"},
    {GLFW_KEY_F3, "F3"},
    {GLFW_KEY_F4, "F4"},
    {GLFW_KEY_F5, "F5"},
    {GLFW_KEY_F6, "F6"},
    {GLFW_KEY_F7, "F7"},
    {GLFW_KEY_F8, "F8"},
    {GLFW_KEY_F9, "F9"},
    {GLFW_KEY_F10, "F10"},
    {GLFW_KEY_F11, "F11"},
    {GLFW_KEY_F12, "F12"},

    {GLFW_KEY_ESCAPE, "ESC"},
    {GLFW_KEY_ENTER, "ENTER"},
    {GLFW_KEY_TAB, "TAB"},
    {GLFW_KEY_BACKSPACE, "BACKSPACE"},
    {GLFW_KEY_INSERT, "INSERT"},
    {GLFW_KEY_DELETE, "DELETE"},
    {GLFW_KEY_RIGHT, "RIGHT"},
    {GLFW_KEY_LEFT, "LEFT"},
    {GLFW_KEY_DOWN, "DOWN"},
    {GLFW_KEY_UP, "UP"},
    {GLFW_KEY_PAGE_UP, "PAGEUP"},
    {GLFW_KEY_PAGE_DOWN, "PAGEDOWN"},
    {GLFW_KEY_HOME, "HOME"},
    {GLFW_KEY_END, "END"},

    {GLFW_KEY_CAPS_LOCK, "CAPSLOCK"},
    {GLFW_KEY_SCROLL_LOCK, "SCROLLLOCK"},
    {GLFW_KEY_NUM_LOCK, "NUMLOCK"},
    {GLFW_KEY_PRINT_SCREEN, "PRINTSCREEN"},
    {GLFW_KEY_PAUSE, "PAUSE"},
    {GLFW_KEY_LEFT_SHIFT, "LSHIFT"},
    {GLFW_KEY_RIGHT_SHIFT, "RSHIFT"},
    {GLFW_KEY_LEFT_CONTROL, "LCTRL"},
    {GLFW_KEY_RIGHT_CONTROL, "RCTRL"},
    {GLFW_KEY_LEFT_ALT, "LALT"},
    {GLFW_KEY_RIGHT_ALT, "RALT"},
    {GLFW_KEY_LEFT_SUPER, "LSUPER"},
    {GLFW_KEY_RIGHT_SUPER, "RSUPER"},
    {GLFW_KEY_MENU, "MENU"},
};

static std::map<std::string, int> nameToKeyMap = {

    {"A", GLFW_KEY_A},
    {"B", GLFW_KEY_B},
    {"C", GLFW_KEY_C},
    {"D", GLFW_KEY_D},
    {"E", GLFW_KEY_E},
    {"F", GLFW_KEY_F},
    {"G", GLFW_KEY_G},
    {"H", GLFW_KEY_H},
    {"I", GLFW_KEY_I},
    {"J", GLFW_KEY_J},
    {"K", GLFW_KEY_K},
    {"L", GLFW_KEY_L},
    {"M", GLFW_KEY_M},
    {"N", GLFW_KEY_N},
    {"O", GLFW_KEY_O},
    {"P", GLFW_KEY_P},
    {"Q", GLFW_KEY_Q},
    {"R", GLFW_KEY_R},
    {"S", GLFW_KEY_S},
    {"T", GLFW_KEY_T},
    {"U", GLFW_KEY_U},
    {"V", GLFW_KEY_V},
    {"W", GLFW_KEY_W},
    {"X", GLFW_KEY_X},
    {"Y", GLFW_KEY_Y},
    {"Z", GLFW_KEY_Z},

    {"0", GLFW_KEY_0},
    {"1", GLFW_KEY_1},
    {"2", GLFW_KEY_2},
    {"3", GLFW_KEY_3},
    {"4", GLFW_KEY_4},
    {"5", GLFW_KEY_5},
    {"6", GLFW_KEY_6},
    {"7", GLFW_KEY_7},
    {"8", GLFW_KEY_8},
    {"9", GLFW_KEY_9},

    {"SPACE", GLFW_KEY_SPACE},
    {"'", GLFW_KEY_APOSTROPHE},
    {",", GLFW_KEY_COMMA},
    {"-", GLFW_KEY_MINUS},
    {".", GLFW_KEY_PERIOD},
    {"/", GLFW_KEY_SLASH},
    {";", GLFW_KEY_SEMICOLON},
    {"=", GLFW_KEY_EQUAL},
    {"[", GLFW_KEY_LEFT_BRACKET},
    {"\\", GLFW_KEY_BACKSLASH},
    {"]", GLFW_KEY_RIGHT_BRACKET},
    {"`", GLFW_KEY_GRAVE_ACCENT},

    {"F1", GLFW_KEY_F1},
    {"F2", GLFW_KEY_F2},
    {"F3", GLFW_KEY_F3},
    {"F4", GLFW_KEY_F4},
    {"F5", GLFW_KEY_F5},
    {"F6", GLFW_KEY_F6},
    {"F7", GLFW_KEY_F7},
    {"F8", GLFW_KEY_F8},
    {"F9", GLFW_KEY_F9},
    {"F10", GLFW_KEY_F10},
    {"F11", GLFW_KEY_F11},
    {"F12", GLFW_KEY_F12},

    {"ESC", GLFW_KEY_ESCAPE},
    {"ENTER", GLFW_KEY_ENTER},
    {"TAB", GLFW_KEY_TAB},
    {"BACKSPACE", GLFW_KEY_BACKSPACE},
    {"INSERT", GLFW_KEY_INSERT},
    {"DELETE", GLFW_KEY_DELETE},
    {"RIGHT", GLFW_KEY_RIGHT},
    {"LEFT", GLFW_KEY_LEFT},
    {"DOWN", GLFW_KEY_DOWN},
    {"UP", GLFW_KEY_UP},
    {"PAGEUP", GLFW_KEY_PAGE_UP},
    {"PAGEDOWN", GLFW_KEY_PAGE_DOWN},
    {"HOME", GLFW_KEY_HOME},
    {"END", GLFW_KEY_END},

    {"CAPSLOCK", GLFW_KEY_CAPS_LOCK},
    {"SCROLLLOCK", GLFW_KEY_SCROLL_LOCK},
    {"NUMLOCK", GLFW_KEY_NUM_LOCK},
    {"PRINTSCREEN", GLFW_KEY_PRINT_SCREEN},
    {"PAUSE", GLFW_KEY_PAUSE},
    {"LSHIFT", GLFW_KEY_LEFT_SHIFT},
    {"RSHIFT", GLFW_KEY_RIGHT_SHIFT},
    {"LCTRL", GLFW_KEY_LEFT_CONTROL},
    {"RCTRL", GLFW_KEY_RIGHT_CONTROL},
    {"LALT", GLFW_KEY_LEFT_ALT},
    {"RALT", GLFW_KEY_RIGHT_ALT},
    {"LSUPER", GLFW_KEY_LEFT_SUPER},
    {"RSUPER", GLFW_KEY_RIGHT_SUPER},
    {"MENU", GLFW_KEY_MENU},
};

void initializeDefaultControls() {
    g_controls.moveForward = GLFW_KEY_W;
    g_controls.moveBackward = GLFW_KEY_S;
    g_controls.moveLeft = GLFW_KEY_A;
    g_controls.moveRight = GLFW_KEY_D;
    g_controls.jumpUp = GLFW_KEY_SPACE;
    g_controls.crouchDown = GLFW_KEY_LEFT_SHIFT;
    g_controls.toggleFlyMode = GLFW_KEY_F;
    g_controls.toggleWireframe = GLFW_KEY_G;
    g_controls.sprint = GLFW_KEY_LEFT_CONTROL;
    g_controls.openInventory = GLFW_KEY_E;
    g_controls.openConsole = GLFW_KEY_T;
    g_controls.zoom = GLFW_KEY_C;
    g_controls.toggleHotbar = GLFW_KEY_F1;
    g_controls.toggleDebug = GLFW_KEY_F3;
    g_controls.toggleFullscreen = GLFW_KEY_F11;
}

std::string getKeyName(int keyCode) {
    auto it = keyNameMap.find(keyCode);
    if (it != keyNameMap.end()) {
        return it->second;
    }
    return "UNKNOWN";
}

int getKeyCodeFromName(const std::string& name) {
    auto it = nameToKeyMap.find(name);
    if (it != nameToKeyMap.end()) {
        return it->second;
    }
    return -999; // Invalid
}

void loadControlsFromFile(const std::string& filename) {
    if (loaded) return;
    
    initializeDefaultControls();
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_WARN("Controls: Controls file not found, using defaults.");
        loaded = true;
        return;
    }

    int loadedCount = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string action, keyName;
        if (std::getline(iss, action, '=') && std::getline(iss, keyName)) {
            // Remove whitespace
            action.erase(0, action.find_first_not_of(" \t"));
            action.erase(action.find_last_not_of(" \t") + 1);
            keyName.erase(0, keyName.find_first_not_of(" \t"));
            keyName.erase(keyName.find_last_not_of(" \t") + 1);
            
            int keyCode = getKeyCodeFromName(keyName);
            if (keyCode == -999) continue; // Skip invalid keys
            
            bool matched = true;
            if (action == "moveForward") g_controls.moveForward = keyCode;
            else if (action == "moveBackward") g_controls.moveBackward = keyCode;
            else if (action == "moveLeft") g_controls.moveLeft = keyCode;
            else if (action == "moveRight") g_controls.moveRight = keyCode;
            else if (action == "jumpUp") g_controls.jumpUp = keyCode;
            else if (action == "crouchDown") g_controls.crouchDown = keyCode;
            else if (action == "toggleFlyMode") g_controls.toggleFlyMode = keyCode;
            else if (action == "toggleWireframe") g_controls.toggleWireframe = keyCode;
            else if (action == "sprint") g_controls.sprint = keyCode;
            else if (action == "openInventory") g_controls.openInventory = keyCode;
            else if (action == "openConsole") g_controls.openConsole = keyCode;
            else if (action == "toggleHotbar") g_controls.toggleHotbar = keyCode;
            else if (action == "toggleDebug") g_controls.toggleDebug = keyCode;
            else if (action == "toggleFullscreen") g_controls.toggleFullscreen = keyCode;
            else if (action == "zoom") g_controls.zoom = keyCode;
            else matched = false;

            if (matched) {
                loadedCount++;
            }
            LOG_DEBUG("Controls: Loaded value: ", action, " = ", keyName);
        }
    }
    LOG_INFO("Controls: Loaded ", loadedCount, " values from ", filename);

    file.close();
    loaded = true;
}

void saveControlsToFile(const std::string& filename) {
    std::ofstream file(filename, std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("Controls: Failed to open ", filename, " for writing.");
        return;
    }

    file << "moveForward=" << getKeyName(g_controls.moveForward) << "\n";
    file << "moveBackward=" << getKeyName(g_controls.moveBackward) << "\n";
    file << "moveLeft=" << getKeyName(g_controls.moveLeft) << "\n";
    file << "moveRight=" << getKeyName(g_controls.moveRight) << "\n";
    file << "jumpUp=" << getKeyName(g_controls.jumpUp) << "\n";
    file << "crouchDown=" << getKeyName(g_controls.crouchDown) << "\n";
    file << "toggleFlyMode=" << getKeyName(g_controls.toggleFlyMode) << "\n";
    file << "toggleWireframe=" << getKeyName(g_controls.toggleWireframe) << "\n";
    file << "sprint=" << getKeyName(g_controls.sprint) << "\n";
    file << "openInventory=" << getKeyName(g_controls.openInventory) << "\n";
    file << "openConsole=" << getKeyName(g_controls.openConsole) << "\n";
    file << "toggleHotbar=" << getKeyName(g_controls.toggleHotbar) << "\n";
    file << "toggleDebug=" << getKeyName(g_controls.toggleDebug) << "\n";
    file << "toggleFullscreen=" << getKeyName(g_controls.toggleFullscreen) << "\n";
    file << "zoom=" << getKeyName(g_controls.zoom) << "\n";

    file.close();
    LOG_INFO("Controls: Saved controls to file: ", filename);
    
    loaded = false;
    loadControlsFromFile(filename);
}
