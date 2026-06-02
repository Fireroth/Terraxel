#include <glad/glad.h>
#include <stb_image.h>
#include "window.hpp"
#include "../core/logger.hpp"
#include "options.hpp"

Window::Window(int width, int height, const char* title)
    : width(width), height(height), title(title), window(nullptr),
      windowedX(100), windowedY(100), windowedWidth(width), windowedHeight(height) {}

Window::~Window() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

void Window::framebufferSizeCallback(GLFWwindow* glfwWindow, int width, int height) {
    glViewport(0, 0, width, height);
    LOG_INFO("Window: Resolution changed: ", width, "x", height);

    Window* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window) {
        window->width = width;
        window->height = height;
        window->aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        if (window->framebufferResizeCallback) {
            window->framebufferResizeCallback(width, height, window->aspectRatio);
        }
    }
}

void Window::init() {
    LOG_INFO("Window: Initializing...");
    if (!glfwInit()) {
        LOG_FATAL("Window: Failed to initialize GLFW");
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWmonitor* monitor = nullptr;
    if (getOptionInt("fullscreen", 0) == 1) {
        monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                width = mode->width;
                height = mode->height;
            }
        }
    }

    window = glfwCreateWindow(width, height, title, monitor, nullptr);
    if (!window) {
        glfwTerminate();
        LOG_FATAL("Window: Failed to create GLFW window");
        exit(EXIT_FAILURE);
    }

    setIcon("textures/icon.png");

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG_FATAL("Window: Failed to initialize GLAD");
        exit(EXIT_FAILURE);
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    glfwSetCursorPos(window, width / 2.0, height / 2.0);
    LOG_INFO("Window: Initialized with resolution ", width, "x", height);
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::clear(float r, float g, float b, float a) const {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Window::swapBuffers() const {
    glfwSwapBuffers(window);
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window);
}

GLFWwindow* Window::getGLFWwindow() const {
    return window;
}

void Window::setIcon(const char* iconPath) {
    GLFWimage image;
    int channels;
    unsigned char* pixels = stbi_load(iconPath, &image.width, &image.height, &channels, 4);
    
    if (pixels) {
        image.pixels = pixels;
        glfwSetWindowIcon(window, 1, &image);
        stbi_image_free(pixels);
    } else {
        LOG_ERROR("Window: Failed to load window icon: ", iconPath);
    }
}

float Window::getAspectRatio() const {
    return aspectRatio;
}

void Window::setFramebufferResizeCallback(std::function<void(int, int, float)> callback) {
    framebufferResizeCallback = callback;
}

void Window::toggleFullscreen() {
    if (!window) return;

    bool isFullscreen = glfwGetWindowMonitor(window) != nullptr;

    if (!isFullscreen) {
        // Save current window position and size
        glfwGetWindowPos(window, &windowedX, &windowedY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                LOG_INFO("Window: Toggled to fullscreen (", mode->width, "x", mode->height, ")");
                setOption("fullscreen", 1.0f);
                saveOptionsToFile("options.txt");
            }
        }
    } else {
        // Exiting fullscreen
        glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, 0);
        LOG_INFO("Window: Toggled to windowed mode (", windowedWidth, "x", windowedHeight, ")");
        setOption("fullscreen", 0.0f);
        saveOptionsToFile("options.txt");
    }

    // Restore VSync setting
    glfwSwapInterval(getOptionInt("vsync", 1));
}