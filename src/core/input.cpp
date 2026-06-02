#include <glad/glad.h>
#include <array>
#include <imgui.h>
#include "input.hpp"
#include "controls.hpp"
#include "window.hpp"
#include "../world/block_interaction.hpp"
#include "../world/world.hpp"
#include "../renderer/imguiOverlay.hpp"

static bool firstMouse = true;
bool cursorCaptured = true;
bool inventoryOpen = false;
bool debugOpen = false;
bool pauseMenuOpen = false;
bool consoleOpen = false;
bool hotbarOpen = true;
bool mainMenuOpen = true;
static Camera* g_camera = nullptr;
static World* g_world = nullptr;
static float lastX;
static float lastY;
static uint16_t selectedBlockType = 1; // Default to grass
bool flyMode = false;
bool wireframeEnabled = false;
bool ingoreInput = false;
bool zoomedIn = false;
int selectedHotbarIndex = 0;
std::array<uint16_t, 9> hotbarBlocks = {1, 2, 3, 4, 5, 14, 7, 8, 16};

bool getZoomState(GLFWwindow*) {
    return zoomedIn;
}

uint16_t getSelectedBlockType() {
    return selectedBlockType;
}

void setSelectedBlockType(uint16_t type) {
    selectedBlockType = type;
}

void setHotbarBlock(int index, uint16_t type) {
    hotbarBlocks[index] = type;
}

const std::array<uint16_t, 9>& getHotbarBlocks() {
    return hotbarBlocks;
}

void setHotbarBlocks(const std::array<uint16_t, 9>& blocks) {
    hotbarBlocks = blocks;
    if (selectedHotbarIndex < 0 || selectedHotbarIndex >= static_cast<int>(hotbarBlocks.size())) {
        selectedHotbarIndex = 0;
    }
    selectedBlockType = hotbarBlocks[selectedHotbarIndex];
}

bool getFlyMode() {
    return flyMode;
}

void setFlyMode(bool enabled) {
    flyMode = enabled;
}

bool getWireframeEnabled() {
    return wireframeEnabled;
}


// Mouse movement
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!cursorCaptured) return;

    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
        return;
    }

    float xOffset = static_cast<float>(xpos) - lastX;
    float yOffset = lastY - static_cast<float>(ypos);

    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    if (g_camera)
        g_camera->processMouseMovement(xOffset, yOffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (pauseMenuOpen) return;
    if (consoleOpen && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
    if (inventoryOpen && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
    const int hotbarSize = 9;

    if (yoffset < 0) {
        selectedHotbarIndex = (selectedHotbarIndex + 1) % hotbarSize;
    } else if (yoffset > 0) {
        selectedHotbarIndex = (selectedHotbarIndex - 1 + hotbarSize) % hotbarSize;
    }

    selectedBlockType = hotbarBlocks[selectedHotbarIndex];
}

// Mouse button callback for block breaking and placing
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (!cursorCaptured) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (g_camera && g_world) {
            placeBreakBlockOnClick(g_world, *g_camera, 'b', selectedBlockType);
        }
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        if (g_camera && g_world) {
            placeBreakBlockOnClick(g_world, *g_camera, 'p', selectedBlockType);
        }
    }
    
    // Middle mouse button: pick block type
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        if (g_camera && g_world) {
            BlockInfo info = getLookedAtBlockInfo(g_world, *g_camera);
            if (info.valid && info.type != 0) {
                setHotbarBlock(selectedHotbarIndex, info.type);
                setSelectedBlockType(info.type);
            }
        }
    }
}

void setupInputCallbacks(GLFWwindow* window, Camera* camera, World* world) {
    g_camera = camera;
    g_world = world;
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    lastX = static_cast<float>(width) / 2.0f;
    lastY = static_cast<float>(height) / 2.0f;
    firstMouse = true;

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetScrollCallback(window, scroll_callback);

    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
}

// Speed multiplier
float getSpeedMultiplier(GLFWwindow* window) {
    bool sprintPressed = glfwGetKey(window, g_controls.sprint) == GLFW_PRESS;
    bool onlyForward = (glfwGetKey(window, g_controls.moveForward) == GLFW_PRESS) && (glfwGetKey(window, g_controls.moveBackward) != GLFW_PRESS);
    bool canSprint = sprintPressed && onlyForward;
    
    if (flyMode)
        return sprintPressed ? 30.0f : 4.0f;
    else
        return canSprint ? 2.25f : 1.75f;
}

// Keyboard movement
void processInput(GLFWwindow* window, Camera& camera, float deltaTime, float speedMultiplier) {
    static bool fullscreenPressedLastFrame = false;
    bool fullscreenPressedThisFrame = glfwGetKey(window, g_controls.toggleFullscreen) == GLFW_PRESS;
    if (fullscreenPressedThisFrame && !fullscreenPressedLastFrame) {
        Window* windowObj = static_cast<Window*>(glfwGetWindowUserPointer(window));
        if (windowObj) {
            windowObj->toggleFullscreen();
        }
    }
    fullscreenPressedLastFrame = fullscreenPressedThisFrame;

    if (mainMenuOpen) return;

    if (g_world)
        if (flyMode)
            camera.updateVelocityFlight(deltaTime);
        else
            camera.updateVelocity(deltaTime, g_world);
    else
        camera.updateVelocity(deltaTime);

    static bool escPressedLastFrame = false;
    bool escPressedThisFrame = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;

    if (escPressedThisFrame && !escPressedLastFrame) {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glfwSetCursorPos(window, width / 2.0, height / 2.0);
        if (inventoryOpen) {
            inventoryOpen = !inventoryOpen;
            cursorCaptured = !cursorCaptured;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if (consoleOpen) {
            ingoreInput = !ingoreInput;
            consoleOpen = !consoleOpen;
            cursorCaptured = !cursorCaptured;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            pauseMenuOpen = !pauseMenuOpen;
            consoleOpen = false;
            inventoryOpen = false;
            cursorCaptured = !cursorCaptured;
            glfwSetInputMode(window, GLFW_CURSOR, cursorCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            firstMouse = true; // Reset mouse position capture
        }
    }
    escPressedLastFrame = escPressedThisFrame;

    if (pauseMenuOpen) return;

    // Toggle debug window
    static bool debugTogglePressedLastFrame = false;
    bool debugTogglePressedThisFrame = glfwGetKey(window, g_controls.toggleDebug) == GLFW_PRESS;
    if (debugTogglePressedThisFrame && !debugTogglePressedLastFrame) {
        debugOpen = !debugOpen;
    }
    debugTogglePressedLastFrame = debugTogglePressedThisFrame;

    // Toggle hotbar
    static bool hotbarTogglePressedLastFrame = false;
    bool hotbarTogglePressedThisFrame = glfwGetKey(window, g_controls.toggleHotbar) == GLFW_PRESS;
    if (hotbarTogglePressedThisFrame && !hotbarTogglePressedLastFrame) {
        hotbarOpen = !hotbarOpen;
    }
    hotbarTogglePressedLastFrame = hotbarTogglePressedThisFrame;

    if (!ingoreInput) { // True if console is opened

        if (glfwGetKey(window, g_controls.moveForward) == GLFW_PRESS)
            camera.processKeyboard("FORWARD", deltaTime, speedMultiplier, flyMode);
        if (glfwGetKey(window, g_controls.moveBackward) == GLFW_PRESS)
            camera.processKeyboard("BACKWARD", deltaTime, speedMultiplier, flyMode);
        if (glfwGetKey(window, g_controls.moveLeft) == GLFW_PRESS)
            camera.processKeyboard("LEFT", deltaTime, speedMultiplier, flyMode);
        if (glfwGetKey(window, g_controls.moveRight) == GLFW_PRESS)
            camera.processKeyboard("RIGHT", deltaTime, speedMultiplier, flyMode);

        if (flyMode) {
            if (glfwGetKey(window, g_controls.crouchDown) == GLFW_PRESS)
                camera.processKeyboard("DOWN", deltaTime, speedMultiplier, flyMode);
            if (glfwGetKey(window, g_controls.jumpUp) == GLFW_PRESS)
                camera.processKeyboard("UP", deltaTime, speedMultiplier, flyMode);
        } else {
            static bool spaceLast = false;
            bool spaceNow = glfwGetKey(window, g_controls.jumpUp) == GLFW_PRESS;
            if (camera.isInLiquidCached()) {
                if (spaceNow) {
                    float verticalSpeedMultiplier = speedMultiplier;
                    if (glfwGetKey(window, g_controls.sprint) == GLFW_PRESS) {
                        verticalSpeedMultiplier *= 1.5f;
                    }
                    camera.processKeyboard("UP", deltaTime, verticalSpeedMultiplier);
                }
                if (glfwGetKey(window, g_controls.crouchDown) == GLFW_PRESS) {
                    camera.processKeyboard("DOWN", deltaTime, speedMultiplier);
                }
            } else {
                if (spaceNow && !spaceLast) {
                    camera.jump();
                }
            }
            spaceLast = spaceNow;
        }

        // Zoom state
        zoomedIn = (glfwGetKey(window, g_controls.zoom) == GLFW_PRESS) ? true : false;

        // Toggle wireframe mode
        static bool wireframePressedLastFrame = false;
        bool wireframePressedThisFrame = glfwGetKey(window, g_controls.toggleWireframe) == GLFW_PRESS;
        if (wireframePressedThisFrame && !wireframePressedLastFrame) {
            wireframeEnabled = !wireframeEnabled;
            showMessage(wireframeEnabled ? "Wireframe mode enabled" : "Wireframe mode disabled", ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 2.0f);
        }
        wireframePressedLastFrame = wireframePressedThisFrame;

        // Toggle fly mode
        static bool flyModePressedLastFrame = false;
        bool flyModePressedThisFrame = glfwGetKey(window, g_controls.toggleFlyMode) == GLFW_PRESS;
        if (flyModePressedThisFrame && !flyModePressedLastFrame) {
            flyMode = !flyMode;
            showMessage(flyMode ? "Fly mode enabled" : "Fly mode disabled", ImVec4(0.2f, 0.8f, 1.0f, 1.0f), 2.0f);
        }
        flyModePressedLastFrame = flyModePressedThisFrame;

        // Toggle console
        static bool consolePressedLastFrame = false;
        bool consolePressedThisFrame = glfwGetKey(window, g_controls.openConsole) == GLFW_PRESS;
        if (consolePressedThisFrame && !consolePressedLastFrame) {
            if (pauseMenuOpen)
                return;
            if (!consoleOpen) {
                consoleOpen = true;
                inventoryOpen = false;

                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                ingoreInput = true;
                cursorCaptured = false;
                firstMouse = true;
            } else {
                consoleOpen = false;

                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                cursorCaptured = true;
                firstMouse = true;
            }
        }
        consolePressedLastFrame = consolePressedThisFrame;

        // Toggle inventory
        static bool inventoryPressedLastFrame = false;
        bool inventoryPressedThisFrame = glfwGetKey(window, g_controls.openInventory) == GLFW_PRESS;
        if (inventoryPressedThisFrame && !inventoryPressedLastFrame) {
            if (!inventoryOpen) {
                inventoryOpen = true;
                consoleOpen = false;

                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                cursorCaptured = false;
                firstMouse = true;
            } else {
                inventoryOpen = false;

                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                cursorCaptured = true;
                firstMouse = true;
            }
        }
        inventoryPressedLastFrame = inventoryPressedThisFrame;

        // Block selection with number keys 1-9
        for (int i = 1; i <= 9; i++) {
            if (glfwGetKey(window, GLFW_KEY_1 + (i - 1)) == GLFW_PRESS) {
                selectedBlockType = hotbarBlocks[i - 1];
                selectedHotbarIndex = i - 1;
            }
        }

    }
}
