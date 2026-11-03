#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "../world/world.hpp"
#include "../world/chunk.hpp"
#include "../core/camera.hpp"
#include "../world/blockDB.hpp"
#include "imguiOverlay.hpp"
#include "renderer.hpp"
#include "../world/block_interaction.hpp"
#include "../core/input.hpp"
#include "../core/options.hpp"
#include "../core/controls.hpp"
#include "../core/window.hpp"
#include "../core/saveManager.hpp"
#include "../core/logger.hpp"
#include "../core/console.hpp"

const float ImGuiOverlay::fpsRefreshInterval = 0.5f; // 500ms

std::vector<const char*> ImGuiOverlay::blockItems;
std::vector<uint16_t> ImGuiOverlay::blockIds;
ImTextureID ImGuiOverlay::texAtlas;
ImTextureID ImGuiOverlay::uiAtlas;

static char inputBuffer[256] = "";
static bool scrollToBottom = false;

struct TemporaryMessage {
    std::string text;
    ImVec4 color;
    float duration;
    float elapsed;
    static constexpr float FADE_TIME = 0.1f;
};

static TemporaryMessage g_toast;
static bool g_toastActive = false;

struct MenuLayout {
    ImVec2 winSize;
    float y;
    float spacing;
    bool useCustomX = false;
    float customXOffset = 0.0f;

    MenuLayout(float contentHeight, float sp = 16.0f) : spacing(sp) {
        winSize = ImGui::GetWindowSize();
        y = (winSize.y - contentHeight) * 0.5f;
    }

    void center(float w) {
        if (useCustomX) {
            ImGui::SetCursorPos(ImVec2(customXOffset, y));
        } else {
            ImGui::SetCursorPos(ImVec2((winSize.x - w) * 0.5f, y));
        }
    }
    void advance(float h) { y += h + spacing; }
    float centerX(float w) const { return (winSize.x - w) * 0.5f; }
};

static void drawMenuTitle(MenuLayout& layout, const char* text) {
    float w = ImGui::CalcTextSize(text).x;
    layout.center(w);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.95f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    layout.advance(ImGui::GetTextLineHeightWithSpacing());
}

enum class MenuButtonTone { Neutral, Primary, Danger, Warning };

static bool drawStyledButton(const char* label, ImVec2 size, MenuButtonTone tone = MenuButtonTone::Neutral) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImU32 face, hoverFace, base;
    switch (tone) {
        case MenuButtonTone::Primary:
            face = IM_COL32(36, 112, 76, 255);
            hoverFace = IM_COL32(52, 150, 96, 255);
            base = IM_COL32(15, 52, 36, 255);
            break;
        case MenuButtonTone::Danger:
            face = IM_COL32(132, 48, 46, 255);
            hoverFace = IM_COL32(174, 62, 58, 255);
            base = IM_COL32(58, 20, 22, 255);
            break;
        case MenuButtonTone::Warning:
            face = IM_COL32(166, 119, 28, 255);
            hoverFace = IM_COL32(211, 157, 43, 255);
            base = IM_COL32(75, 49, 12, 255);
            break;
        default:
            face = IM_COL32(43, 47, 61, 255);
            hoverFace = IM_COL32(61, 68, 88, 255);
            base = IM_COL32(15, 17, 24, 255);
            break;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float depth = 4.0f;
    const float pressOffset = held ? 2.0f : 0.0f;
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    const ImVec2 faceMin(pos.x, pos.y + pressOffset);
    const ImVec2 faceMax(end.x, end.y - depth + pressOffset);
    const ImU32 currentFace = hovered ? hoverFace : face;

    draw->AddRectFilled(pos, end, base);
    draw->AddRectFilled(faceMin, faceMax, currentFace);
    draw->AddRectFilled(ImVec2(faceMin.x, faceMin.y), ImVec2(faceMax.x, faceMin.y + 2.0f), IM_COL32(255, 255, 255, hovered ? 52 : 30));

    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const ImVec2 labelPos(pos.x + (size.x - labelSize.x) * 0.5f, faceMin.y + (faceMax.y - faceMin.y - labelSize.y) * 0.5f);
    draw->AddText(ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, 150), label);
    draw->AddText(labelPos, IM_COL32(240, 242, 248, 255), label);
    return clicked;
}

static bool drawMenuButton(MenuLayout& layout, const char* label, ImVec2 size) {
    layout.center(size.x);
    MenuButtonTone tone = MenuButtonTone::Neutral;
    if (std::strcmp(label, "Resume") == 0 || std::strcmp(label, "Create World") == 0)
        tone = MenuButtonTone::Primary;
    else if (std::strcmp(label, "Quit Game") == 0)
        tone = MenuButtonTone::Danger;

    const bool clicked = drawStyledButton(label, size, tone);
    layout.advance(size.y);
    return clicked;
}

static bool drawMenuToggle(MenuLayout& layout, const char* label, bool* value, ImVec2 size) {
    char displayText[128];
    snprintf(displayText, sizeof(displayText), "%s: %s", label, *value ? "ON" : "OFF");

    layout.center(size.x);
    const bool toggled = drawStyledButton(displayText, size, *value ? MenuButtonTone::Primary : MenuButtonTone::Danger);
    if (toggled)
        *value = !*value;

    layout.advance(size.y);
    return toggled;
}

static bool drawMenuSliderInt(MenuLayout& layout, const char* label, const char* id, int* value, int vmin, int vmax, float sliderWidth = 300.0f, const char* valueFmt = "%d", const char* const* valueLabels = nullptr) {
    layout.center(sliderWidth);

    const int NORM_MAX = 1000;
    int normVal = (vmax > vmin) ? (int)roundf((*value - vmin) * (float)NORM_MAX / (float)(vmax - vmin)) : 0;
    normVal = normVal < 0 ? 0 : (normVal > NORM_MAX ? NORM_MAX : normVal);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.17f, 0.18f, 0.24f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.85f, 0.85f, 0.85f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 1.00f, 1.00f, 0.80f));
    ImGui::PushItemWidth(sliderWidth);

    bool changed = ImGui::SliderInt(id, &normVal, 0, NORM_MAX, "");
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);

    if (changed) {
        int mapped = vmin + (int)roundf(normVal * (float)(vmax - vmin) / (float)NORM_MAX);
        mapped = mapped < vmin ? vmin : (mapped > vmax ? vmax : mapped);
        *value = mapped;
    }

    ImVec2 rMin = ImGui::GetItemRectMin();
    ImVec2 rMax = ImGui::GetItemRectMax();
    char displayText[128];
    char valBuf[64];
    if (valueLabels)
        snprintf(valBuf, sizeof(valBuf), "%s", valueLabels[*value - vmin]);
    else
        snprintf(valBuf, sizeof(valBuf), valueFmt, *value);
    snprintf(displayText, sizeof(displayText), "%s: %s", label, valBuf);
    ImVec2 textSize = ImGui::CalcTextSize(displayText);
    ImVec2 textPos(
        rMin.x + (rMax.x - rMin.x - textSize.x) * 0.5f,
        rMin.y + (rMax.y - rMin.y - textSize.y) * 0.5f
    );
    ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(255, 255, 255, 230), displayText);

    layout.advance(rMax.y - rMin.y);
    return changed;
}

static bool drawMenuSliderFloat(MenuLayout& layout, const char* label, const char* id, float* value, float vmin, float vmax, float sliderWidth = 300.0f, const char* valueFmt = "%.1f") {
    layout.center(sliderWidth);

    float normVal = (vmax > vmin) ? (*value - vmin) / (vmax - vmin) : 0.0f;
    normVal = normVal < 0.0f ? 0.0f : (normVal > 1.0f ? 1.0f : normVal);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.17f, 0.18f, 0.24f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.85f, 0.85f, 0.85f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 1.00f, 1.00f, 0.80f));
    ImGui::PushItemWidth(sliderWidth);

    bool changed = ImGui::SliderFloat(id, &normVal, 0.0f, 1.0f, "");
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);

    if (changed) {
        float mapped = vmin + normVal * (vmax - vmin);
        mapped = mapped < vmin ? vmin : (mapped > vmax ? vmax : mapped);
        *value = mapped;
    }

    ImVec2 rMin = ImGui::GetItemRectMin();
    ImVec2 rMax = ImGui::GetItemRectMax();
    char displayText[128];
    char valBuf[64];
    snprintf(valBuf, sizeof(valBuf), valueFmt, *value);
    snprintf(displayText, sizeof(displayText), "%s: %s", label, valBuf);
    ImVec2 textSize = ImGui::CalcTextSize(displayText);
    ImVec2 textPos(
        rMin.x + (rMax.x - rMin.x - textSize.x) * 0.5f,
        rMin.y + (rMax.y - rMin.y - textSize.y) * 0.5f
    );
    ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(255, 255, 255, 230), displayText);

    layout.advance(rMax.y - rMin.y);
    return changed;
}

static void pushMenuInputStyle(float width = 0.0f) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.06f, 0.07f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.17f, 0.18f, 0.24f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.25f, 0.39f, 0.62f, 0.35f));
    if (width != 0.0f) ImGui::PushItemWidth(width);
}

static void popMenuInputStyle(bool hasWidth = false) {
    if (hasWidth) ImGui::PopItemWidth();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
}

static void pushMenuStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.40f, 0.55f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.30f, 0.45f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.40f, 0.45f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.15f, 0.20f, 0.65f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.15f, 0.15f, 0.20f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.85f, 0.85f, 0.85f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 1.00f, 1.00f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 2.0f);
}

static void popMenuStyle() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(9);
}


ImGuiOverlay::ImGuiOverlay()
    : fpsTimer(0.0f), frameCount(0), fpsDisplay(0.0f) {
    prevPauseMenuOpen = false;
    prevConsoleOpen = false;
    consoleFocusDelayFrames = 0;
}

ImGuiOverlay::~ImGuiOverlay() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

bool ImGuiOverlay::init(GLFWwindow* window, GLuint textureAtlas, GLuint uiAtlasTexture) {
    LOG_INFO("ImGuiOverlay: Initializing...");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "./config/imgui.ini";
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    updateBlockDBItems();

    texAtlas = (ImTextureID)(intptr_t)textureAtlas;
    uiAtlas = (ImTextureID)(intptr_t)uiAtlasTexture;

    // Generate 3D block preview textures for inventory andhotbar
    BlockPreviewRenderer::init(textureAtlas);
    BlockPreviewRenderer::generatePreviews();

    Console::init();

    LOG_INFO("ImGuiOverlay: Loading font");
    ImFont* Font = io.Fonts->AddFontFromFileTTF("assets/Font.ttf", 25.0f);
    if (!Font) {
        LOG_ERROR("ImGuiOverlay: Failed to load font from ./assets/Font.ttf");
    }

    return true;
}

void ImGuiOverlay::updateBlockDBItems() {
    blockItems.clear();
    blockIds.clear();
    tabMap.clear();
    tabOrder.clear();

    for (uint16_t id : BlockDB::getRegisteredBlockIDs()) {
        if (id == 0) continue;
        const auto* info = BlockDB::getBlockInfo(id);
        if (info) {
            blockItems.push_back(info->name.c_str());
            blockIds.push_back(id);
        }
    }

    // Keep tab insertion order consistent across platforms
    for (size_t i = 0; i < blockItems.size(); i++) {
        const auto* blockInfo = BlockDB::getBlockInfo(blockIds[i]);
        auto& indices = tabMap[blockInfo->tabName];
        if (indices.empty())
            tabOrder.push_back(blockInfo->tabName);
        indices.push_back(i);
    }
}

void ImGuiOverlay::render(float deltaTime, Camera& camera, World* world, Renderer* renderer) {
    static int lastSelectedHotbarIndex = -1;
    static uint16_t lastSelectedBlockId = 0;

    frameCount++;
    fpsTimer += deltaTime;

    if (fpsTimer >= fpsRefreshInterval) {
        fpsDisplay = frameCount / fpsTimer;
        frameCount = 0;
        fpsTimer = 0.0f;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Main menu screen
    if (mainMenuOpen) {
        lastSelectedHotbarIndex = -1;
        renderMainMenu(camera, world, renderer);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }

    // Pause menu screen
    if (pauseMenuOpen && !mainMenuOpen) {
        renderPauseMenu(camera, world, renderer);
    }

    prevPauseMenuOpen = pauseMenuOpen;

    // Debug window
    if (debugOpen) {
        renderDebugWindow(camera, world, renderer, deltaTime);
    }

    // Inventory window
    if (inventoryOpen) {
        renderInventory();
    }

    // Hotbar UI
    if (hotbarOpen && !pauseMenuOpen) {
        renderHotbar();
    }

    // Console window
    if (consoleOpen) {
        renderConsole(camera, world, renderer);
    }

    prevConsoleOpen = consoleOpen;

    // Temporary messages
    if (hotbarOpen && !pauseMenuOpen) {
        renderMessage(deltaTime);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void showMessage(const std::string& text, ImVec4 color, float duration) {
    g_toast = { text, color, duration, 0.0f };
    g_toastActive = true;
}

void ImGuiOverlay::renderMessage(float deltaTime) {
    if (!g_toastActive)
        return;

    g_toast.elapsed += deltaTime;

    if (g_toast.elapsed >= g_toast.duration) {
        g_toastActive = false;
        return;
    }

    float alpha = 1.0f;
    const float ft = TemporaryMessage::FADE_TIME;

    if (g_toast.elapsed < ft) {
        // Fade in
        alpha = g_toast.elapsed / ft;
    } else if (g_toast.elapsed > g_toast.duration - ft) {
        // Fade out
        alpha = (g_toast.duration - g_toast.elapsed) / ft;
    }

    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 textSize = ImGui::CalcTextSize(g_toast.text.c_str());
    const float padX = 5.0f;
    const float padY = 5.0f;
    const float winW = textSize.x + padX * 2.0f;
    const float winH = textSize.y + padY * 2.0f;
    const float bottomGap = 95.0f;

    ImVec2 winPos(io.DisplaySize.x * 0.5f - winW * 0.5f, io.DisplaySize.y - winH - bottomGap);

    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.62f * alpha);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.09f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));

    ImGui::Begin("##toast",
        nullptr,
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoDecoration);

    ImVec4 textCol = g_toast.color;
    textCol.w *= alpha;
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
    ImGui::TextUnformatted(g_toast.text.c_str());
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(1);
}

static int parseSeed(const char* seedBuf) {
    std::string s(seedBuf);
    if (s.empty()) {
        std::random_device rd;
        return static_cast<int>(rd());
    }
    try {
        return std::stoi(s);
    } catch (std::exception& e) {
        LOG_ERROR("ImGuiOverlay: Error parsing seed: ", e.what());
        return static_cast<int>(std::hash<std::string>{}(s));
    }
}

void ImGuiOverlay::enterWorld(const WorldInfo& info, World* world, Camera& camera, Renderer* renderer) {
    SaveManager::setActiveWorld(info);
    world->reset();

    glm::dvec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::array<uint16_t, 9> hotbar;
    bool flyEnabled = false;
    bool isSneaking = false;
    float savedTimeOfDay = 6000.0f;
    bool savedDayCycleEnabled = true;

    if (SaveManager::loadPlayerState(position, yaw, pitch, hotbar, flyEnabled, isSneaking, savedTimeOfDay, savedDayCycleEnabled)) {
        int playerChunkX = static_cast<int>(std::floor(position.x / Chunk::chunkWidth));
        int playerChunkZ = static_cast<int>(std::floor(position.z / Chunk::chunkDepth));
        world->generateChunks(2, playerChunkX, playerChunkZ);
        camera.setPosition(position);
        camera.setRotation(yaw, pitch);
        camera.setSneaking(isSneaking, world, true);
        setHotbarBlocks(hotbar);
        setFlyMode(flyEnabled);
        if (renderer) {
            renderer->timeOfDay = savedTimeOfDay;
            renderer->dayCycleEnabled = savedDayCycleEnabled;
        }
    } else {
        setHotbarBlocks({1, 2, 3, 4, 5, 14, 7, 8, 16});
        camera.setPosition(world->findSpawnPosition());
        setFlyMode(false);
        if (renderer) {
            renderer->timeOfDay = 6000.0f;
            renderer->dayCycleEnabled = true;
        }
    }

    mainMenuOpen = false;
    pauseMenuOpen = false;
    cursorCaptured = true;
    glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void ImGuiOverlay::renderPauseMenu(Camera& camera, World* world, Renderer* renderer) {
    if (!prevPauseMenuOpen)
        pauseScreenPage = PauseMenuPage::Main;

    debugOpen = false;
    inventoryOpen = false;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("Pause Menu",
                nullptr,
                ImGuiWindowFlags_NoTitleBar | 
                ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_NoMove | 
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus | 
                ImGuiWindowFlags_NoNavFocus);
    
    const ImVec2 buttonSize(220, 45);
    const float spacing = 16.0f;
    const float titleH = ImGui::GetTextLineHeightWithSpacing();
    const float sliderH = ImGui::GetFrameHeight();

    pushMenuStyle();

    switch (pauseScreenPage) {
        case PauseMenuPage::Main: {
            float totalH = titleH + spacing + 4 * buttonSize.y + 3 * spacing;
            MenuLayout layout(totalH, spacing);

            drawMenuTitle(layout, "Game Paused");

            if (drawMenuButton(layout, "Resume", buttonSize)) {
                pauseMenuOpen = false;
                cursorCaptured = true;
                glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }

            if (drawMenuButton(layout, "Settings", buttonSize))
                pauseScreenPage = PauseMenuPage::Settings;

            if (drawMenuButton(layout, "Quit to Title", buttonSize)) {
                SaveManager::savePlayerState(
                    camera.getPositionDouble(),
                    camera.getYaw(),
                    camera.getPitch(),
                    getHotbarBlocks(),
                    getFlyMode(),
                    camera.getIsSneaking(),
                    renderer->timeOfDay,
                    renderer->dayCycleEnabled
                );
                world->reset();
                SaveManager::clearActiveWorld();
                mainMenuOpen = true;
                pauseMenuOpen = false;
                cursorCaptured = false;
                glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }

            if (drawMenuButton(layout, "Quit Game", buttonSize))
                glfwSetWindowShouldClose(glfwGetCurrentContext(), true);
        } break;

        case PauseMenuPage::Settings: {
            float totalH = titleH + spacing + 4 * buttonSize.y + 3 * spacing;
            MenuLayout layout(totalH, spacing);

            drawMenuTitle(layout, "Settings");

            if (drawMenuButton(layout, "Video", buttonSize))
                pauseScreenPage = PauseMenuPage::Video;

            if (drawMenuButton(layout, "World", buttonSize))
                pauseScreenPage = PauseMenuPage::World;

            if (drawMenuButton(layout, "Controls", buttonSize))
                pauseScreenPage = PauseMenuPage::ControlsCustomize;

            if (drawMenuButton(layout, "Back", buttonSize))
                pauseScreenPage = PauseMenuPage::Main;
        } break;

        case PauseMenuPage::Video: {
            float sliderHeight = buttonSize.y;
            float totalH = titleH + spacing + 9 * (sliderHeight + spacing) + buttonSize.y;
            MenuLayout layout(totalH, spacing);

            drawMenuTitle(layout, "Video Settings");

            float columnsStartY = layout.y;
            float columnWidth = 300.0f;
            float columnGap = 20.0f;
            float leftColumnX = layout.winSize.x * 0.5f - columnWidth - columnGap * 0.5f;
            float rightColumnX = layout.winSize.x * 0.5f + columnGap * 0.5f;

            // --- Left Column ---
            layout.useCustomX = true;
            layout.customXOffset = leftColumnX;

            int renderDistance = getOptionInt("render_distance");
            if (drawMenuSliderInt(layout, "Render Distance", "##RenderDistance", &renderDistance, 1, 64)) {
                setOption("render_distance", static_cast<float>(renderDistance));
                world->updateChunksAroundPlayer(camera.getPosition(), renderDistance + 1, true);
            }

            int chunks_to_load_per_frame = getOptionInt("chunks_to_load_per_frame");
            if (drawMenuSliderInt(layout, "Chunks to Load per Frame", "##ChunksToLoadPerFrame", &chunks_to_load_per_frame, 1, 16)) {
                setOption("chunks_to_load_per_frame", static_cast<float>(chunks_to_load_per_frame));
            }

            int chunks_to_sort_per_frame = getOptionInt("chunks_to_sort_per_frame");
            if (drawMenuSliderInt(layout, "Chunks to Sort per Frame", "##ChunksToSortPerFrame", &chunks_to_sort_per_frame, 1, 16)) {
                setOption("chunks_to_sort_per_frame", static_cast<float>(chunks_to_sort_per_frame));
            }

            float fov = getOptionFloat("fov");
            if (drawMenuSliderFloat(layout, "Field of View", "##FOV", &fov, 10.0f, 110.0f)) {
                setOption("fov", static_cast<float>(fov));
            }

            int maxFps = getOptionInt("max_fps");
            int sliderVal = (maxFps == 0) ? 250 : maxFps;
            const char* fpsFormat = (sliderVal >= 250) ? "Unlimited" : "%d";
            if (drawMenuSliderInt(layout, "Max FPS", "##MaxFPS", &sliderVal, 10, 250, 300.0f, fpsFormat)) {
                setOption("max_fps", static_cast<float>((sliderVal >= 250) ? 0 : sliderVal));
            }

            bool lightingEnabled = renderer->lightingEnabled;
            if (drawMenuToggle(layout, "Enable Lighting", &lightingEnabled, ImVec2(300, buttonSize.y))) {
                renderer->lightingEnabled = lightingEnabled;
                setOption("enable_lighting", static_cast<float>(lightingEnabled ? 1 : 0));
                world->reset();
            }

            bool fasterModelsEnabled = getOptionInt("faster_models") != 0;
            if (drawMenuToggle(layout, "Faster Models", &fasterModelsEnabled, ImVec2(300, buttonSize.y))) {
                setOption("faster_models", static_cast<float>(fasterModelsEnabled ? 1 : 0));
                world->reset();
                BlockDB::init();
                updateBlockDBItems();
                BlockPreviewRenderer::generatePreviews();
            }

            static const char* cloudModeLabels[] = { "OFF", "2D", "3D" };
            if (drawMenuSliderInt(layout, "Clouds", "##Clouds", &renderer->cloudsMode, 0, 2, 300.0f, "%d", cloudModeLabels)) {
                setOption("clouds", static_cast<float>(renderer->cloudsMode));
            }

            if (drawMenuSliderInt(layout, "Cloud Distance", "##CloudRenderDistance", &renderer->cloudRenderDistance, 4, 96, 300.0f, "%d chunks")) {
                setOption("cloud_render_distance", static_cast<float>(renderer->cloudRenderDistance));
            }

            float leftColumnEndY = layout.y;

            // --- Right Column ---
            layout.y = columnsStartY;
            layout.customXOffset = rightColumnX;

            float brightnessPercent = getOptionFloat("brightness") * 400.0f;
            if (drawMenuSliderFloat(layout, "Brightness", "##Brightness", &brightnessPercent, 0.0f, 100.0f, 300.0f, "%.0f%%")) {
                float val = brightnessPercent / 400.0f;
                setOption("brightness", val);
                renderer->minBrightness = val;
            }

            int use_mipmaps = getOptionInt("use_mipmaps");
            static const char* mipmapLabels[] = { "Off", "Nearest", "Bilinear", "Trilinear" };
            if (drawMenuSliderInt(layout, "Mipmaps", "##UseMipmaps", &use_mipmaps, 0, 3, 300.0f, "%d", mipmapLabels)) {
                setOption("use_mipmaps", static_cast<float>(use_mipmaps));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "Controls the mipmap filtering mode:\n"
                    "Off = No mipmaps\n"
                    "Nearest = Nearest mipmap filtering (pixelated)\n"
                    "Bilinear = Nearest mipmap level + linear filtering inside mipmaps\n"
                    "Trilinear = Linear filtering inside mipmaps + blending between mipmaps (slowest)"
                );
                ImGui::EndTooltip();
            }

            int mipmap_levels = getOptionInt("mipmap_levels");
            if (drawMenuSliderInt(layout, "Mipmap Levels", "##MipmapLevels", &mipmap_levels, 1, 4)) {
                setOption("mipmap_levels", static_cast<float>(mipmap_levels));
            }

            float lod_bias = getOptionFloat("lod_bias");
            if (drawMenuSliderFloat(layout, "LOD Bias", "##LODBias", &lod_bias, -1.0f, 1.0f)) {
                setOption("lod_bias", static_cast<float>(lod_bias));
            }

            if (drawMenuToggle(layout, "Fog", &renderer->fogEnabled, ImVec2(300, buttonSize.y))) {
                setOption("fog", static_cast<float>(renderer->fogEnabled ? 1 : 0));
            }

            if (drawMenuToggle(layout, "Vignette", &renderer->vignetteEnabled, ImVec2(300, buttonSize.y))) {
                setOption("vignette", static_cast<float>(renderer->vignetteEnabled ? 1 : 0));
            }

            bool vsyncEnabled = getOptionInt("vsync") != 0;
            if (drawMenuToggle(layout, "VSync", &vsyncEnabled, ImVec2(300, buttonSize.y))) {
                setOption("vsync", static_cast<float>(vsyncEnabled ? 1 : 0));
                glfwSwapInterval(vsyncEnabled ? 1 : 0);
            }

            bool fullscreenEnabled = glfwGetWindowMonitor(glfwGetCurrentContext()) != nullptr;
            if (drawMenuToggle(layout, "Fullscreen", &fullscreenEnabled, ImVec2(300, buttonSize.y))) {
                Window* windowObj = static_cast<Window*>(glfwGetWindowUserPointer(glfwGetCurrentContext()));
                if (windowObj) {
                    windowObj->toggleFullscreen();
                }
            }

            bool aoEnabled = getOptionInt("ambient_occlusion") != 0;
            if (drawMenuToggle(layout, "Ambient Occlusion", &aoEnabled, ImVec2(300, buttonSize.y))) {
                setOption("ambient_occlusion", static_cast<float>(aoEnabled ? 1 : 0));
                world->reset();
            }

            float rightColumnEndY = layout.y;

            // --- Back Button ---
            layout.y = std::max(leftColumnEndY, rightColumnEndY);
            layout.useCustomX = false;

            if (drawMenuButton(layout, "Back", buttonSize)) {
                saveOptionsToFile("config/options.txt");
                pauseScreenPage = PauseMenuPage::Settings;
            }
        } break;

        case PauseMenuPage::World: {
            float sliderHeight = buttonSize.y;
            float totalH = titleH + spacing + 2 * (sliderHeight + spacing) + buttonSize.y;
            MenuLayout layout(totalH, spacing);

            drawMenuTitle(layout, "World Settings");

            if (renderer) {
                float currentTime = renderer->timeOfDay;
                if (drawMenuSliderFloat(layout, "Time", "##WorldTimeOfDay", &currentTime, 0.0f, 24000.0f, 300.0f, "%.0f")) {
                    renderer->timeOfDay = currentTime;
                }

                bool cycleEnabled = renderer->dayCycleEnabled;
                if (drawMenuToggle(layout, "Day Cycle", &cycleEnabled, ImVec2(300, buttonSize.y))) {
                    renderer->dayCycleEnabled = cycleEnabled;
                }
            }

            if (drawMenuButton(layout, "Back", buttonSize)) {
                pauseScreenPage = PauseMenuPage::Settings;
            }
        } break;

        case PauseMenuPage::ControlsCustomize: {
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 winSize = ImGui::GetWindowSize();
            float controlsWidth = 600.0f;
            float controlsHeight = io.DisplaySize.y * 0.62f;
            float bottomBtnH = 40.0f;
            float sliderHeight = 45.0f;
            float totalH = controlsHeight + spacing + sliderHeight + spacing + bottomBtnH;
            MenuLayout layout(totalH, spacing);

            layout.center(controlsWidth);
            ImGui::BeginChild("ControlsCustomize", ImVec2(controlsWidth, controlsHeight), true);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
            ImGui::Text("Customize Controls");
            ImGui::Separator();

            static int selectedControlIndex = -1;
            static bool waitingForKey = false;
            static int pendingKeyControl = -1;

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
            ImGui::Text("Click on a control to rebind it, then press the new key");
            ImGui::Spacing();

            auto displayControlRow = [&](const char* label, int& controlKey, int controlIndex) {
                bool isSelected = (selectedControlIndex == controlIndex && waitingForKey);
                const float buttonWidth = controlsWidth - 20.0f;
                const float contentWidth = ImGui::GetContentRegionAvail().x;
                const float leftPadding = std::max(0.0f, (contentWidth - buttonWidth) * 0.5f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftPadding);

                std::string displayText = std::string(label) + ": " + getKeyName(controlKey);
                if (drawStyledButton(displayText.c_str(), ImVec2(buttonWidth, 30),
                                     isSelected ? MenuButtonTone::Primary : MenuButtonTone::Neutral)) {
                    selectedControlIndex = controlIndex;
                    waitingForKey = true;
                    pendingKeyControl = controlIndex;
                }

                if (isSelected) {
                    ImGui::Indent(10.0f);
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Press a key...");
                    ImGui::Unindent(10.0f);
                }

                if (isSelected) {
                    GLFWwindow* window = glfwGetCurrentContext();
                    for (int key = 0; key < GLFW_KEY_LAST; key++) {
                        if (glfwGetKey(window, key) == GLFW_PRESS) {
                            if (key == GLFW_KEY_ESCAPE) {
                                waitingForKey = false;
                                selectedControlIndex = -1;
                            } else {
                                controlKey = key;
                                waitingForKey = false;
                                selectedControlIndex = -1;
                            }
                            break;
                        }
                    }
                }
            };

            displayControlRow("Move Forward", g_controls.moveForward, 0);
            displayControlRow("Move Backward", g_controls.moveBackward, 1);
            displayControlRow("Move Left", g_controls.moveLeft, 2);
            displayControlRow("Move Right", g_controls.moveRight, 3);
            displayControlRow("Jump/Up", g_controls.jumpUp, 4);
            displayControlRow("Crouch/Down", g_controls.crouchDown, 5);
            displayControlRow("Sprint", g_controls.sprint, 6);
            displayControlRow("Toggle Fly Mode", g_controls.toggleFlyMode, 7);
            displayControlRow("Toggle Wireframe", g_controls.toggleWireframe, 8);
            displayControlRow("Open Inventory", g_controls.openInventory, 9);
            displayControlRow("Open Console", g_controls.openConsole, 10);
            displayControlRow("Toggle Hotbar", g_controls.toggleHotbar, 11);
            displayControlRow("Toggle Debug", g_controls.toggleDebug, 12);
            displayControlRow("Zoom", g_controls.zoom, 13);
            displayControlRow("Toggle Fullscreen", g_controls.toggleFullscreen, 14);

            ImGui::EndChild();
            layout.advance(controlsHeight);

            int mouseSensitivity = getOptionInt("mouse_sensitivity");
            if (drawMenuSliderInt(layout, "Mouse Sensitivity", "##MouseSensitivity", &mouseSensitivity, 1, 100)) {
                setOption("mouse_sensitivity", static_cast<float>(mouseSensitivity));
            }

            float btnW = 140.0f;
            float btnGap = 8.0f;
            float totalBtnW = 3 * btnW + 2 * btnGap;
            layout.center(totalBtnW);

            if (drawStyledButton("Load Defaults", ImVec2(btnW, bottomBtnH), MenuButtonTone::Neutral)) {
                initializeDefaultControls();
            }
            ImGui::SameLine(0.0f, btnGap);
            if (drawStyledButton("Save & Back", ImVec2(btnW, bottomBtnH), MenuButtonTone::Primary)) {
                saveControlsToFile("config/controls.txt");
                saveOptionsToFile("config/options.txt");
                pauseScreenPage = PauseMenuPage::Settings;
            }
            ImGui::SameLine(0.0f, btnGap);
            if (drawStyledButton("Cancel", ImVec2(btnW, bottomBtnH), MenuButtonTone::Danger)) {
                pauseScreenPage = PauseMenuPage::Settings;
            }
        } break;
    }

    popMenuStyle();

    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void ImGuiOverlay::renderDebugWindow(Camera& camera, World* world, Renderer* renderer, float deltaTime) {
    ImGui::SetNextWindowSize(ImVec2(450, 0));
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    
    glm::dvec3 pos = camera.getPositionDouble();
    glm::vec3 front = camera.getFront();
    glm::vec3 up = camera.getUp();
    BlockInfo blockInfo = getLookedAtBlockInfo(world, camera);
    float camYaw = camera.getYaw();
    float camPitch = camera.getPitch();
    bool grounded = camera.isGrounded();
    bool inLiquid = camera.isInLiquidCached();
    uint16_t eyeBlock = camera.getEyeBlock(world);

    uint16_t selectedBlockType = getSelectedBlockType();
    glm::dvec3 velocity = camera.getVelocity();
    float speedHor = static_cast<float>(glm::length(glm::dvec2(velocity.x, velocity.z)));
    float speedVer = static_cast<float>(glm::length(velocity.y));
    float speed = static_cast<float>(glm::length(velocity));
    
    float eyeHeight = camera.getEyeHeight();
    glm::dvec3 feetPos = glm::dvec3(pos.x, pos.y - eyeHeight, pos.z);
    int chunkX = static_cast<int>(std::floor(feetPos.x / 16.0f));
    int chunkZ = static_cast<int>(std::floor(feetPos.z / 16.0f));

    int eyeBlockX = static_cast<int>(std::floor(pos.x));
    int eyeBlockY = static_cast<int>(std::floor(pos.y));
    int eyeBlockZ = static_cast<int>(std::floor(pos.z));
    int eyeChunkX = worldToChunkCoord(eyeBlockX, Chunk::chunkWidth);
    int eyeChunkZ = worldToChunkCoord(eyeBlockZ, Chunk::chunkDepth);
    Chunk* eyeChunk = world->getChunk(eyeChunkX, eyeChunkZ);
    uint8_t eyeSkyLight = 0;
    uint8_t eyeBlockLight = 0;
    if (getOptionInt("enable_lighting") == 0) {
        eyeSkyLight = 15;
        eyeBlockLight = 0;
    } else if (eyeChunk) {
        int eyeLocalX = eyeBlockX - eyeChunkX * Chunk::chunkWidth;
        int eyeLocalY = eyeBlockY;
        int eyeLocalZ = eyeBlockZ - eyeChunkZ * Chunk::chunkDepth;
        if (eyeLocalX >= 0 && eyeLocalX < Chunk::chunkWidth &&
            eyeLocalY >= 0 && eyeLocalY < Chunk::chunkHeight &&
            eyeLocalZ >= 0 && eyeLocalZ < Chunk::chunkDepth) {
            eyeSkyLight = eyeChunk->getSkyLight(eyeLocalX, eyeLocalY, eyeLocalZ);
            eyeBlockLight = eyeChunk->getBlockLight(eyeLocalX, eyeLocalY, eyeLocalZ);
        } else if (eyeLocalY >= Chunk::chunkHeight) {
            eyeSkyLight = 15;
        }
    } else if (eyeBlockY >= Chunk::chunkHeight) {
        eyeSkyLight = 15;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.7f));

    ImGui::Begin("Debug",
                nullptr,
                ImGuiWindowFlags_NoTitleBar | 
                ImGuiWindowFlags_NoMove | 
                ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowFontScale(0.85f);

    ImGui::Text("FPS: %.1f", fpsDisplay);
    ImGui::Text("Pos: %.2f / %.2f / %.2f", feetPos.x-0.5, feetPos.y, feetPos.z-0.5);
    ImGui::Text("Time: %.0f", renderer ? renderer->timeOfDay : 0.0f);
    ImGui::Text("Delta Time: %.2f ms", deltaTime*1000);
    ImGui::Text("Chunk: %d, %d", chunkX, chunkZ);
    ImGui::Text("Chunk Biome: %s", eyeChunk && eyeChunk->biomeIndex >= 0 ? BiomeDB::getBiome(eyeChunk->biomeIndex) ? BiomeDB::getBiome(eyeChunk->biomeIndex)->name.c_str() : "Unknown" : "Unknown");
    ImGui::Text("Message elapsed time: %.5f", g_toast.elapsed);
    ImGui::Separator();
    ImGui::Text("Camera -> Yaw: %.2f", camYaw);
    ImGui::Text("Camera -> Pitch: %.2f", camPitch);
    ImGui::Text("Camera -> Front: %.2f, %.2f, %.2f", front.x, front.y, front.z);
    ImGui::Text("Camera -> Up: %.2f, %.2f, %.2f", up.x, up.y, up.z);
    ImGui::Text("Camera -> Speed (Horizontal): %.2f", speedHor);
    ImGui::Text("Camera -> Speed (Vertical): %.2f", speedVer);
    ImGui::Text("Camera -> Speed (Total): %.2f", speed);
    ImGui::Text("Camera -> Grounded: %s", grounded ? "True" : "False");
    ImGui::Text("Camera -> In Liquid: %s", inLiquid ? "True" : "False");
    ImGui::Text("Camera -> Eye Block: %d", eyeBlock);
    ImGui::Text("Camera -> Sky Light: %d", eyeSkyLight);
    ImGui::Text("Camera -> Block Light: %d", eyeBlockLight);
    ImGui::Separator();
    ImGui::Text("Input -> Selected: %s", BlockDB::getBlockInfo(selectedBlockType)->name.c_str());
    ImGui::Text("Input -> Selected ID: %d", selectedBlockType);
    ImGui::Separator();
    if (blockInfo.valid) {
        const auto* info = BlockDB::getBlockInfo(blockInfo.type);
        ImGui::Text("Block -> Name: %s", info->name.c_str());
        ImGui::Text("Block -> ID: %d", blockInfo.type);
        ImGui::Text("Block -> Pos: %d / %d / %d", blockInfo.worldPos.x, blockInfo.worldPos.y, blockInfo.worldPos.z);
        ImGui::Text("Block -> Transparent: %s", info->transparent ? "True" : "False");
        ImGui::Text("Block -> Translucent: %s", info->translucent ? "True" : "False");
        ImGui::Text("Block -> Render Faces In Between: %s", info->renderFacesInBetween ? "True" : "False");
        ImGui::Text("Block -> Model Name: %s", info->modelName.c_str());
        ImGui::Text("Block -> Light Emission: %i", info->lightEmission);
        ImGui::Text("Block -> Sky Light: %d", blockInfo.skyLight);
        ImGui::Text("Block -> Block Light: %d", blockInfo.blockLight);
        ImGui::Text("Block -> Drag: %.1f", info->drag);

    } else {
        ImGui::Text("Block -> Name: Air");
        ImGui::Text("Block -> ID: 0");
    }
    ImGui::PopStyleColor(2);
    ImGui::End();
}

void ImGuiOverlay::renderInventory() {
    ImGuiIO& io = ImGui::GetIO();

    const float slotSize = 64.0f;
    const float slotPad = 6.0f;
    const float slotGap = 4.0f;
    const uint8_t itemsPerRow = 7;
    const float gridWidth = itemsPerRow * (slotSize + slotPad * 2 + slotGap) - slotGap + 16.0f;
    const float winWidth = gridWidth + 20.0f;
    const float winHeight = io.DisplaySize.y * 0.65f;

    ImVec2 invPos(io.DisplaySize.x * 0.5f - winWidth * 0.5f, io.DisplaySize.y * 0.5f - winHeight * 0.5f);
    ImGui::SetNextWindowPos(invPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winWidth, winHeight), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.17f, 0.18f, 0.24f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));

    ImGui::Begin("Inventory",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.11f, 0.13f, 0.17f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.20f, 0.51f, 0.83f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.20f, 0.51f, 0.83f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0.20f, 0.59f, 0.38f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);

    if (ImGui::BeginTabBar("InventoryTabs")) {
        for (const auto& tabName : tabOrder) {
            if (tabName == "Internal" && !getOptionInt("show_internal_tab")) {
                continue;
            }
            auto& indices = tabMap[tabName];
            if (ImGui::BeginTabItem(tabName.c_str())) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(0,0,0,0));
                ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
                ImGui::BeginChild("BlockGrid", ImVec2(0, 0), false, 0);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(slotPad, slotPad));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(slotGap, slotGap));

                {
                    const float totalSlotW = slotSize + slotPad * 2.0f;
                    const float rowW = itemsPerRow * totalSlotW + (itemsPerRow - 1) * slotGap;
                    const float availW = ImGui::GetContentRegionAvail().x;
                    const float offsetX = std::max(0.0f, (availW - rowW) * 0.5f);
                    ImGui::Indent(offsetX);
                }

                for (size_t n = 0; n < indices.size(); n++) {
                    size_t i = indices[n];

                    GLuint previewTex = BlockPreviewRenderer::getPreviewTexture(blockIds[i]);
                    ImTextureID texId = previewTex ? (ImTextureID)(intptr_t)previewTex : texAtlas;
                    ImVec2 uv0, uv1;

                    if (previewTex) {
                        uv0 = ImVec2(0.0f, 1.0f);
                        uv1 = ImVec2(1.0f, 0.0f);
                    } else {
                        const auto* blockInfo = BlockDB::getBlockInfo(blockIds[i]);
                        int tileX = static_cast<int>(blockInfo->textureCoords[0].x);
                        int tileY = static_cast<int>(blockInfo->textureCoords[0].y);
                        uv0 = ImVec2((tileX * 16) / 256.0f, ((16 - tileY) * 16) / 256.0f);
                        uv1 = ImVec2(((tileX + 1) * 16) / 256.0f, ((15 - tileY) * 16) / 256.0f);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.15f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.51f, 0.83f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.51f, 0.83f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.30f, 0.42f, 1.0f));
                    

                    ImGui::ImageButton(blockItems[i], texId, ImVec2(slotSize, slotSize), uv0, uv1);
                    
                    if (ImGui::IsItemActivated()){
                        setHotbarBlock(selectedHotbarIndex, blockIds[i]);
                        setSelectedBlockType(blockIds[i]);
                    }

                    ImGui::PopStyleColor(4);

                    if (ImGui::IsItemHovered()) {
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
                        ImGui::BeginTooltip();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                        ImGui::TextUnformatted(blockItems[i]);
                        ImGui::PopStyleColor();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.70f, 1.0f));
                        ImGui::Text("ID: %d", blockIds[i]);
                        ImGui::PopStyleColor();
                        ImGui::EndTooltip();
                        ImGui::PopStyleVar();
                    }

                    if ((n % itemsPerRow) != (itemsPerRow - 1) && n + 1 < indices.size())
                        ImGui::SameLine();
                }
                ImGui::PopStyleVar(4);

                {
                    const float totalSlotW = slotSize + slotPad * 2.0f;
                    const float rowW = itemsPerRow * totalSlotW + (itemsPerRow - 1) * slotGap;
                    const float availW = ImGui::GetContentRegionAvail().x;
                    const float offsetX = std::max(0.0f, (availW - rowW) * 0.5f);
                    ImGui::Unindent(offsetX);
                }

                ImGui::EndChild();
                ImGui::PopStyleVar(1); 
                ImGui::PopStyleColor(4);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(4);
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void ImGuiOverlay::renderHotbar() {
    static int lastSelectedHotbarIndex = -1;
    static uint16_t lastSelectedBlockId = 0;

    ImGuiIO& io = ImGui::GetIO();
    const float slotSize = 54.0f;
    const float slotPad = 6.0f;
    const float slotGap = 4.0f;
    const int numSlots = (int)hotbarBlocks.size();
    const float winPadX = 10.0f;
    const float winPadY = 8.0f;
    const float totalSlotW = slotSize + slotPad * 2.0f;
    const float winWidth = numSlots * totalSlotW + (numSlots - 1) * slotGap + winPadX * 2.0f;
    const float winHeight = totalSlotW + winPadY * 2.0f;
    const float bottomGap = 12.0f;

    ImVec2 hotbarPos(io.DisplaySize.x * 0.5f - winWidth * 0.5f, io.DisplaySize.y - winHeight - bottomGap);

    if (lastSelectedHotbarIndex == -1) {
        lastSelectedHotbarIndex = selectedHotbarIndex;
        lastSelectedBlockId = hotbarBlocks[selectedHotbarIndex];
    }

    if (selectedHotbarIndex != lastSelectedHotbarIndex || hotbarBlocks[selectedHotbarIndex] != lastSelectedBlockId) {
        lastSelectedHotbarIndex = selectedHotbarIndex;
        lastSelectedBlockId = hotbarBlocks[selectedHotbarIndex];
        const auto* blockInfo = BlockDB::getBlockInfo(lastSelectedBlockId);
        if (blockInfo) {
            showMessage(blockInfo->name, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 1.5f);
        }
    }

    ImGui::SetNextWindowPos(hotbarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winWidth, winHeight), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.09f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(winPadX, winPadY));

    ImGui::Begin("Hotbar",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(slotPad, slotPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(slotGap, slotGap));

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (size_t i = 0; i < hotbarBlocks.size(); i++) {
        uint16_t blockId = hotbarBlocks[i];
        const auto* blockInfo = BlockDB::getBlockInfo(blockId);
        if (!blockInfo) continue;

        GLuint previewTex = BlockPreviewRenderer::getPreviewTexture(blockId);
        ImTextureID texId = previewTex ? (ImTextureID)(intptr_t)previewTex : texAtlas;
        ImVec2 uv0, uv1;

        if (previewTex) {
            uv0 = ImVec2(0.0f, 1.0f);
            uv1 = ImVec2(1.0f, 0.0f);
        } else {
            int tileX = static_cast<int>(blockInfo->textureCoords[0].x);
            int tileY = static_cast<int>(blockInfo->textureCoords[0].y);
            uv0 = ImVec2((tileX * 16) / 256.0f, ((16 - tileY) * 16) / 256.0f);
            uv1 = ImVec2(((tileX + 1) * 16) / 256.0f, ((15 - tileY) * 16) / 256.0f);
        }

        bool isSelected = (i == selectedHotbarIndex);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.40f, 0.68f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.51f, 0.83f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.51f, 0.83f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.70f, 1.00f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.15f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.21f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.12f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.30f, 0.42f, 1.0f));
        }

        std::string buttonLabel = "##hotbar_" + std::to_string(i);
        ImGui::ImageButton(buttonLabel.c_str(), texId, ImVec2(slotSize, slotSize), uv0, uv1);
        ImGui::PopStyleColor(4);

        ImVec2 rMin = ImGui::GetItemRectMin();
        char numBuf[4];
        snprintf(numBuf, sizeof(numBuf), "%d", (int)(i + 1) % 10);
        ImVec2 numPos(rMin.x + 5.0f, rMin.y + 3.0f);
        drawList->AddText(ImVec2(numPos.x + 1.0f, numPos.y + 1.0f), IM_COL32(0, 0, 0, 180), numBuf);
        drawList->AddText(numPos, IM_COL32(220, 224, 232, 200), numBuf);

        if (isSelected) {
            ImVec2 rMin = ImGui::GetItemRectMin();
            ImVec2 rMax = ImGui::GetItemRectMax();
            drawList->AddRect(rMin, rMax, IM_COL32(52, 132, 211, 255), 2.0f, 0, 2.0f);
        }

        if (i < hotbarBlocks.size() - 1)
            ImGui::SameLine(0.0f, slotGap);
    }

    ImGui::PopStyleVar(4);
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void ImGuiOverlay::renderConsole(Camera& camera, World* world, Renderer* renderer) {
    ImGuiIO& io = ImGui::GetIO();
    float consoleWidth = io.DisplaySize.x;
    float consoleHeight = io.DisplaySize.y * 0.6f;
    ImVec2 consolePos(0, 0);

    ImGui::SetNextWindowPos(consolePos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(consoleWidth, consoleHeight), ImGuiCond_Always);

    ImGui::Begin("ConsoleWindow",
                nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse);

    ImGui::BeginChild("consoleLog", ImVec2(0, consoleHeight - 60), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::SetWindowFontScale(0.85f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    for (const auto& line : Console::getLog()) {
        ImGui::TextWrapped("%s", line.c_str());
    }
    ImGui::PopStyleVar();
    if (scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom = false;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::PushItemWidth(-1);

    if (!prevConsoleOpen && consoleOpen) {
        consoleFocusDelayFrames = 5;
    }

    if (consoleFocusDelayFrames > 0) {
        consoleFocusDelayFrames--;
        if (consoleFocusDelayFrames == 0) {
            ImGui::SetKeyboardFocusHere();
        }
    }

    bool enterPressed = ImGui::InputText("##ConsoleInput", inputBuffer, IM_ARRAYSIZE(inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

    if (enterPressed) {
        std::string input(inputBuffer);
        if (!input.empty()) {
            Console::execute(input, camera, world, renderer);
            inputBuffer[0] = '\0';
            scrollToBottom = true;
        }
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void ImGuiOverlay::renderMainMenu(Camera& camera, World* world, Renderer* renderer) {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("MainMenu",
                nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);

    pushMenuStyle();

    const float bgScale = std::max(0.90f, io.DisplaySize.y / 1080.0f);
    const float bgTileW = 133.0f * 12.0f * bgScale;
    const float bgTileH = 20.0f * 12.0f * bgScale;
    const float bgScrollSpeed = 60.0f;
    const float bgBackScrollSpeed = 30.0f;
    const float bgFarScrollSpeed = 15.0f;

    static float bgScroll = 0.0f;
    static float bgBackScroll = 0.0f;
    static float bgFarScroll = 0.0f;

    bgScroll = std::fmod(bgScroll + io.DeltaTime * bgScrollSpeed, bgTileW);
    bgBackScroll = std::fmod(bgBackScroll + io.DeltaTime * bgBackScrollSpeed, bgTileW);
    bgFarScroll = std::fmod(bgFarScroll + io.DeltaTime * bgFarScrollSpeed, bgTileW);

    const float atlasTexel = 1.0f / 256.0f;
    const float atlasPad = 0.1f * atlasTexel;
    const ImVec2 bgUv0(123.0f / 256.0f + atlasPad, 1.0f - atlasPad);
    const ImVec2 bgUv1(1.0f - atlasPad, 1.0f - 20.0f / 256.0f + atlasPad);
    const ImVec2 bgBackUv0(123.0f / 256.0f + atlasPad, 1.0f - (20.0f + 1.0f) / 256.0f - atlasPad);
    const ImVec2 bgBackUv1(1.0f - atlasPad, 1.0f - (20.0f + 1.0f + 20.0f) / 256.0f + atlasPad);
    const ImVec2 bgFarUv0(123.0f / 256.0f + atlasPad, 1.0f - (20.0f + 1.0f + 20.0f + 1.0f) / 256.0f - atlasPad);
    const ImVec2 bgFarUv1(1.0f - atlasPad, 1.0f - (20.0f + 1.0f + 20.0f + 1.0f + 20.0f) / 256.0f + atlasPad);
    const float bgY = io.DisplaySize.y - bgTileH;
    const float bgBackY = bgY - 85.0f;
    const float bgFarY = bgBackY - 130.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float gradientCutoff = std::min(400.0f, io.DisplaySize.y * 0.25f);
    drawList->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(io.DisplaySize.x, io.DisplaySize.y - gradientCutoff),
        IM_COL32(14, 13, 26, 255),
        IM_COL32(14, 13, 26, 255),
        IM_COL32(83, 52, 50, 255),
        IM_COL32(83, 52, 50, 255));

    for (float x = -bgTileW; x < io.DisplaySize.x + bgTileW; x += bgTileW) {
        drawList->AddImage(uiAtlas, ImVec2(x - bgFarScroll, bgFarY), ImVec2(x + bgTileW - bgFarScroll, bgFarY + bgTileH), bgFarUv0, bgFarUv1);
    }
    for (float x = -bgTileW; x < io.DisplaySize.x + bgTileW; x += bgTileW) {
        drawList->AddImage(uiAtlas, ImVec2(x - bgBackScroll, bgBackY), ImVec2(x + bgTileW - bgBackScroll, bgBackY + bgTileH), bgBackUv0, bgBackUv1);
    }
    for (float x = -bgTileW; x < io.DisplaySize.x + bgTileW; x += bgTileW) {
        drawList->AddImage(uiAtlas, ImVec2(x - bgScroll, bgY), ImVec2(x + bgTileW - bgScroll, bgY + bgTileH), bgUv0, bgUv1);
    }

    const ImVec2 buttonSize(260, 45);
    const float spacing = 16.0f;

    switch (mainMenuPage) {
        case MainMenuPage::WorldList: {
            // Title
            {
                const ImVec2 titleSize(94.0f * 5, 15.0f * 5);
                ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - titleSize.x) * 0.5f, 30.0f));
                ImGui::Image(uiAtlas, titleSize, ImVec2(0.0f, 1.0f), ImVec2(94.0f/256.0f, 1.0f - 15.0f/256.0f));
            }

            float listTop = 110.0f;
            float listBottom = io.DisplaySize.y - 120.0f;
            float listWidth = 560.0f;
            float listX = (io.DisplaySize.x - listWidth) * 0.5f;

            ImGui::SetCursorPos(ImVec2(listX, listTop));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.09f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.17f, 0.18f, 0.24f, 1.00f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
            ImGui::BeginChild("WorldList", ImVec2(listWidth, listBottom - listTop), true);

            auto worlds = SaveManager::listWorlds();

            if (worlds.empty()) {
                ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - 10.0f);
                float textW = ImGui::CalcTextSize("No worlds yet. Create one!").x;
                ImGui::SetCursorPosX((listWidth - textW) * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
                ImGui::TextUnformatted("No worlds yet. Create one!");
                ImGui::PopStyleColor();
            } else {
                for (auto& w : worlds) {
                    ImGui::PushID(w.uuid.c_str());

                    float entryHeight = 75.0f;
                    float availWidth = ImGui::GetContentRegionAvail().x;

                    ImVec2 panelPos = ImGui::GetCursorScreenPos();
                    ImVec2 panelEnd(panelPos.x + availWidth, panelPos.y + entryHeight);
                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    bool cardHovered = ImGui::IsMouseHoveringRect(panelPos, panelEnd);
                    
                    ImU32 cardBg = cardHovered ? IM_COL32(22, 25, 34, 255) : IM_COL32(14, 16, 22, 255);
                    ImU32 cardBorder = cardHovered ? IM_COL32(52, 112, 166, 200) : IM_COL32(32, 35, 46, 255);

                    dl->AddRectFilled(panelPos, panelEnd, cardBg, 2.0f);
                    dl->AddRect(panelPos, panelEnd, cardBorder, 2.0f);

                    dl->AddText(ImVec2(panelPos.x + 12, panelPos.y + 8), IM_COL32(255, 255, 255, 230), w.name.c_str());

                    const float metaFontSize = std::max(12.0f, std::round(ImGui::GetFontSize() * 0.75f));
                    char seedBuf[64];
                    snprintf(seedBuf, sizeof(seedBuf), "Seed: %d", w.seed);
                    dl->AddText(ImGui::GetFont(), metaFontSize, ImVec2(panelPos.x + 12, panelPos.y + 30), IM_COL32(180, 180, 190, 200), seedBuf);

                    char dateBuf[64];
                    snprintf(dateBuf, sizeof(dateBuf), "Created: %s", w.createdAt.c_str());
                    dl->AddText(ImGui::GetFont(), metaFontSize, ImVec2(panelPos.x + 12, panelPos.y + 48), IM_COL32(140, 140, 150, 180), dateBuf);

                    float btnW = 90.0f;
                    float btnH = 30.0f;
                    float playBtnW = 120.0f;
                    float playBtnH = 40.0f;
                    float btnGap = 5.0f;
                    float btnsX = panelPos.x + availWidth - playBtnW - btnW - btnGap - 10.0f;
                    float btnsY = panelPos.y + (entryHeight - (2 * btnH + btnGap)) * 0.5f;

                    ImGui::SetCursorScreenPos(ImVec2(btnsX, panelPos.y + (entryHeight - playBtnH) * 0.5f));
                    if (drawStyledButton("Play", ImVec2(playBtnW, playBtnH), MenuButtonTone::Primary)) {
                        enterWorld(w, world, camera, renderer);
                    }

                    ImGui::SetCursorScreenPos(ImVec2(btnsX + playBtnW + btnGap, btnsY));
                    if (drawStyledButton("Edit", ImVec2(btnW, btnH), MenuButtonTone::Warning)) {
                        editingWorldUUID = w.uuid;
                        snprintf(editingWorldNameBuf, sizeof(editingWorldNameBuf), "%s", w.name.c_str());
                        mainMenuPage = MainMenuPage::EditWorld;
                        deleteConfirmUUID = "";
                    }

                    ImGui::SetCursorScreenPos(ImVec2(btnsX + playBtnW + btnGap, btnsY + btnH + btnGap));
                    if (drawStyledButton("Delete", ImVec2(btnW, btnH), MenuButtonTone::Danger)) {
                        deleteConfirmUUID = w.uuid;
                        deleteConfirmName = w.name;
                        mainMenuPage = MainMenuPage::DeleteConfirm;
                    }

                    ImGui::SetCursorScreenPos(ImVec2(panelPos.x, panelPos.y + entryHeight));
                    ImGui::Dummy(ImVec2(availWidth, 6.0f));

                    ImGui::PopID();
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);

            // Bottom buttons
            float bottomY = io.DisplaySize.y - 100.0f;
            float totalBtnWidth = buttonSize.x * 2 + spacing;
            float btnStartX = (io.DisplaySize.x - totalBtnWidth) * 0.5f;

            ImGui::SetCursorPos(ImVec2(btnStartX, bottomY));
            if (drawStyledButton("Create New World", buttonSize, MenuButtonTone::Primary)) {
                mainMenuPage = MainMenuPage::CreateWorld;
                snprintf(worldNameBuf, sizeof(worldNameBuf), "New World");
                worldSeedBuf[0] = '\0';
                deleteConfirmUUID = "";
            }

            ImGui::SetCursorPos(ImVec2(btnStartX + buttonSize.x + spacing, bottomY));
            if (drawStyledButton("Quit Game", buttonSize, MenuButtonTone::Danger)) {
                glfwSetWindowShouldClose(glfwGetCurrentContext(), true);
            }
        } break;

        case MainMenuPage::CreateWorld: {
            float inputHeight = 40.0f;
            float labelHeight = 20.0f;
            float totalH = ImGui::GetTextLineHeightWithSpacing() + spacing
                         + 2 * (labelHeight + inputHeight + spacing)
                         + 2 * buttonSize.y + spacing;
            MenuLayout layout(totalH, spacing);

            drawMenuTitle(layout, "Create New World");

            // World name label
            float inputWidth = 300.0f;
            layout.center(inputWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.78f, 0.82f, 0.9f));
            ImGui::TextUnformatted("World Name");
            ImGui::PopStyleColor();
            layout.advance(labelHeight);

            // World name input
            layout.center(inputWidth);
            pushMenuInputStyle(inputWidth);
            ImGui::InputText("##WorldName", worldNameBuf, sizeof(worldNameBuf));
            popMenuInputStyle(true);
            layout.advance(inputHeight);

            // Seed label
            layout.center(inputWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.78f, 0.82f, 0.9f));
            ImGui::TextUnformatted("Seed (empty = random)");
            ImGui::PopStyleColor();
            layout.advance(labelHeight);

            // Seed input
            layout.center(inputWidth);
            pushMenuInputStyle(inputWidth);
            ImGui::InputText("##WorldSeed", worldSeedBuf, sizeof(worldSeedBuf));
            popMenuInputStyle(true);
            layout.advance(inputHeight);

            // Create & Play button
            if (drawMenuButton(layout, "Create & Play", buttonSize)) {
                std::string name(worldNameBuf);
                if (name.empty()) name = "New World";
                int seed = parseSeed(worldSeedBuf);
                WorldInfo info = SaveManager::createWorld(name, seed);
                enterWorld(info, world, camera, renderer);
                mainMenuPage = MainMenuPage::WorldList;
            }

            // Back button
            if (drawMenuButton(layout, "Back", buttonSize)) {
                mainMenuPage = MainMenuPage::WorldList;
            }
        } break;

        case MainMenuPage::EditWorld: {
            float inputHeight = 40.0f;
            float labelHeight = 20.0f;
            float totalH = ImGui::GetTextLineHeightWithSpacing() + spacing + labelHeight + inputHeight + spacing + 2 * buttonSize.y + spacing;
            MenuLayout layout(totalH, spacing);

            drawMenuTitle(layout, "Edit World");

            // World name label
            float inputWidth = 300.0f;
            layout.center(inputWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.78f, 0.82f, 0.9f));
            ImGui::TextUnformatted("World Name");
            ImGui::PopStyleColor();
            layout.advance(labelHeight);

            // World name input
            layout.center(inputWidth);
            pushMenuInputStyle(inputWidth);
            ImGui::InputText("##EditWorldName", editingWorldNameBuf, sizeof(editingWorldNameBuf));
            popMenuInputStyle(true);
            layout.advance(inputHeight);

            // Save button
            if (drawMenuButton(layout, "Save", buttonSize)) {
                SaveManager::renameWorld(editingWorldUUID, editingWorldNameBuf);
                editingWorldUUID = "";
                mainMenuPage = MainMenuPage::WorldList;
            }

            // Back button
            if (drawMenuButton(layout, "Back", buttonSize)) {
                editingWorldUUID = "";
                mainMenuPage = MainMenuPage::WorldList;
            }
        } break;

        case MainMenuPage::DeleteConfirm: {
            float titleH = ImGui::GetTextLineHeightWithSpacing();
            float lineH = ImGui::GetTextLineHeight();
            float totalH = titleH + spacing + 3 * lineH + 3 * spacing + 2 * buttonSize.y + spacing;
            MenuLayout layout(totalH, spacing);

            drawMenuTitle(layout, "Delete World");

            std::string line1 = "Are you sure you want to delete world";
            layout.center(ImGui::CalcTextSize(line1.c_str()).x);
            ImGui::TextUnformatted(line1.c_str());
            layout.advance(lineH);

            std::string line2 = "\"" + deleteConfirmName + "\"?";
            layout.center(ImGui::CalcTextSize(line2.c_str()).x);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            ImGui::TextUnformatted(line2.c_str());
            ImGui::PopStyleColor();
            layout.advance(lineH);

            std::string line3 = "This action cannot be undone!";
            layout.center(ImGui::CalcTextSize(line3.c_str()).x);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.35f, 0.35f, 1.0f));
            ImGui::TextUnformatted(line3.c_str());
            ImGui::PopStyleColor();
            layout.advance(lineH);

            layout.center(buttonSize.x);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.15f, 0.15f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.18f, 0.18f, 1.0f));
            if (ImGui::Button("Yes, Delete", buttonSize)) {
                SaveManager::deleteWorld(deleteConfirmUUID);
                deleteConfirmUUID = "";
                deleteConfirmName = "";
                mainMenuPage = MainMenuPage::WorldList;
            }
            ImGui::PopStyleColor(3);
            layout.advance(buttonSize.y);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.40f, 0.15f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.55f, 0.20f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.45f, 0.18f, 1.0f));
            if (drawMenuButton(layout, "Cancel", buttonSize)) {
                deleteConfirmUUID = "";
                deleteConfirmName = "";
                mainMenuPage = MainMenuPage::WorldList;
            }
            ImGui::PopStyleColor(3);
        } break;
    }

    popMenuStyle();

    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}