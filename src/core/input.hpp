#pragma once
#include <GLFW/glfw3.h>
#include <array>
#include "camera.hpp"

uint8_t getSelectedBlockType();
void setSelectedBlockType(uint8_t type);
void setHotbarBlock(int index, uint8_t type);
const std::array<uint8_t, 9>& getHotbarBlocks();
void setHotbarBlocks(const std::array<uint8_t, 9>& blocks);
bool getFlyMode();
void setFlyMode(bool enabled);
void setupInputCallbacks(GLFWwindow* window, Camera* camera, class World* world);
void processInput(GLFWwindow* window, Camera& camera, float deltaTime, float speedMultiplier);
float getSpeedMultiplier(GLFWwindow* window);
bool getZoomState(GLFWwindow* window);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);