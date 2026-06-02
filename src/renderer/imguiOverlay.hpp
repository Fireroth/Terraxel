#pragma once

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>
#include "../core/camera.hpp"
#include "blockPreviewRenderer.hpp"

class Renderer;

extern bool inventoryOpen;
extern bool debugOpen;
extern bool pauseMenuOpen;
extern bool cursorCaptured;
extern bool consoleOpen;
extern bool hotbarOpen;
extern bool mainMenuOpen;

extern int selectedHotbarIndex;
extern std::array<uint16_t, 9> hotbarBlocks;
void showMessage(const std::string& text, ImVec4 color, float duration);

class ImGuiOverlay {
public:
    ImGuiOverlay();
    ~ImGuiOverlay();

    bool init(GLFWwindow* window, GLuint textureAtlas, GLuint uiAtlasTexture);
    void updateBlockDBItems();
    void render(float deltaTime, Camera& camera, class World* world, Renderer* renderer);

    static std::vector<const char*> blockItems;
    static std::vector<uint16_t> blockIds;
    static ImTextureID texAtlas;
    static ImTextureID uiAtlas;

    std::unordered_map<std::string, std::vector<size_t>> tabMap;
    std::vector<std::string> tabOrder;

    enum class PauseMenuPage {
        Main,
        Settings,
        Video,
        ControlsCustomize
    };

    enum class MainMenuPage {
        WorldList,
        CreateWorld,
        EditWorld,
        DeleteConfirm
    };

    PauseMenuPage pauseScreenPage = PauseMenuPage::Main;
    MainMenuPage mainMenuPage = MainMenuPage::WorldList;
    
    bool prevPauseMenuOpen = false;
    bool prevConsoleOpen = false;
    int consoleFocusDelayFrames = 0;

    char worldNameBuf[64] = "New World";
    char worldSeedBuf[32] = "";
    std::string deleteConfirmUUID = "";
    std::string deleteConfirmName = "";

    std::string editingWorldUUID = "";
    char editingWorldNameBuf[64] = "";

private:
    float fpsTimer;
    int frameCount;
    float fpsDisplay;
    static const float fpsRefreshInterval;

    void renderMainMenu(Camera& camera, World* world, Renderer* renderer);
    void enterWorld(const struct WorldInfo& info, World* world, Camera& camera);
    void renderMessage(float deltaTime);
    void renderPauseMenu(Camera& camera, World* world, Renderer* renderer);
    void renderDebugWindow(Camera& camera, World* world, float deltaTime);
    void renderInventory();
    void renderHotbar();
    void renderConsole(Camera& camera, World* world);
};
