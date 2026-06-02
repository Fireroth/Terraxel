#pragma once

#include <GLFW/glfw3.h>
#include <array>
#include "camera.hpp"

uint16_t getSelectedBlockType();
void setSelectedBlockType(uint16_t type);
void setHotbarBlock(int index, uint16_t type);
const std::array<uint16_t, 9>& getHotbarBlocks();
void setHotbarBlocks(const std::array<uint16_t, 9>& blocks);
bool getFlyMode();
void setFlyMode(bool enabled);
void setupInputCallbacks(GLFWwindow* window, Camera* camera, class World* world);
void processInput(GLFWwindow* window, Camera& camera, float deltaTime, float speedMultiplier);
float getSpeedMultiplier(GLFWwindow* window);
bool getZoomState(GLFWwindow* window);
bool getWireframeEnabled();
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);