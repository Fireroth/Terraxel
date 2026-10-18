#pragma once

#include <vector>
#include <string>
#include <functional>
#include "camera.hpp"

class World;
class Renderer;

struct ConsoleCommand {
    std::string name;
    std::string usage;
    std::string description;
    std::function<void(const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world, Renderer* renderer)> handler;
};

class Console {
public:
    static void init();
    static void execute(const std::string& input, Camera& camera, World* world, Renderer* renderer = nullptr);
    static void write(const std::string& line);
    static void clear();
    static const std::vector<std::string>& getLog();

private:
    static std::vector<ConsoleCommand> commands;
    static std::vector<std::string> logLines;
};
