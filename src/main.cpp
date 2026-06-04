#ifdef _WIN32
    #include <windows.h>
    #include <timeapi.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <thread>
#include <chrono>
#include <glad/glad.h>
#include "renderer/imguiOverlay.hpp"
#include "renderer/blockPreviewRenderer.hpp"
#include "core/window.hpp"
#include "renderer/renderer.hpp"
#include "core/camera.hpp"
#include "core/input.hpp"
#include "core/options.hpp"
#include "core/controls.hpp"
#include "core/saveManager.hpp"
#include "world/modelDB.hpp"
#include "world/biomeDB.hpp"
#include "world/structureDB.hpp"
#include "core/logger.hpp"

GLFWwindow* g_currentGLFWwindow = nullptr;
GLFWwindow* getCurrentGLFWwindow() { return g_currentGLFWwindow; }
const char* banner = R"(Welcome to Terraxel!
 _____                            _ 
|_   _|                          | |
  | | ___ _ __ _ __ __ ___  _____| |
  | |/ _ \ '__| '__/ _` \ \/ / _ \ |
  | |  __/ |  | | | (_| |>  <  __/ |
  \_/\___|_|  |_|  \__,_/_/\_\___|_|
)";

int main() {
    LOG_INFO(banner);

    loadOptionsFromFile("options.txt");
    loadControlsFromFile("controls.txt");

    int windowWidth = getOptionInt("window_width", 1280);
    int windowHeight = getOptionInt("window_height", 720);
    float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;

    #ifdef _WIN32
        HWND hwnd = GetConsoleWindow();
        ShowWindow(hwnd, getOptionInt("hide_console", 1) ? SW_HIDE : SW_SHOW);
        timeBeginPeriod(1);
    #endif

    Renderer renderer;
    ImGuiOverlay ImGuiOverlay;
    BlockDB::init();
    BiomeDB::init();
    ModelDB::init();
    StructureDB::init();
    
    Camera camera(
        glm::vec3(0.5f, 70.0f, 0.5f),  // Position
        glm::vec3(0.0f, 1.0f, 0.0f),   // Up vector
        0.0f,                          // Yaw
        0.0f                           // Pitch
    );

    Window window(windowWidth, windowHeight, "Terraxel");
    window.init();

    window.setFramebufferResizeCallback([&aspectRatio](int w, int h, float ar) {
        aspectRatio = ar;
    });

    GLFWwindow* glfwWindow = window.getGLFWwindow();
    g_currentGLFWwindow = glfwWindow;

    setupInputCallbacks(glfwWindow, &camera, &renderer.world);

    // Start in main menu with cursor visible
    glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    cursorCaptured = false;

    glfwSwapInterval(getOptionInt("vsync", 1));
    
    renderer.init();
    ImGuiOverlay.init(glfwWindow, renderer.textureAtlas2D, renderer.uiAtlas);
    
    // Main game loop
    LOG_INFO("Main: Initialization complete, entering game loop");
    while (!window.shouldClose()) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(glfwWindow, camera, deltaTime, getSpeedMultiplier(glfwWindow));

        if (mainMenuOpen) {
            window.clear(0.08f, 0.08f, 0.12f, 1.0f);
        } else {
            renderer.renderWorld(camera, aspectRatio, deltaTime, currentFrame);
            renderer.renderCrosshair(aspectRatio);
        }

        ImGuiOverlay.render(deltaTime, camera, &renderer.world, &renderer);

        window.swapBuffers();
        window.pollEvents();

        // Frame rate limiting
        int currentMaxFPS = getOptionInt("max_fps", 60);
        if (currentMaxFPS > 0) {
            double targetFrameTime = 1.0 / static_cast<double>(currentMaxFPS);
            double frameEnd = currentFrame + targetFrameTime;
            double remaining = frameEnd - glfwGetTime();
            while (remaining > 0.002) {
                #ifdef _WIN32
                    Sleep(1);
                #else
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                #endif
                remaining = frameEnd - glfwGetTime();
            }
            while (glfwGetTime() < frameEnd) {}
        }
    }

    #ifdef _WIN32
        timeEndPeriod(1);
    #endif

    SaveManager::savePlayerState(
        camera.getPositionDouble(),
        camera.getYaw(),
        camera.getPitch(),
        getHotbarBlocks(),
        getFlyMode()
    );

    BlockPreviewRenderer::cleanup();

    LOG_INFO("Main: Exiting game");
    return 0;
}
