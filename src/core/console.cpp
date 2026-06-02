#include <glad/glad.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <cmath>
#include "console.hpp"
#include "options.hpp"
#include "logger.hpp"
#include "input.hpp"
#include "../renderer/imguiOverlay.hpp"
#include "../world/world.hpp"
#include "../world/chunk.hpp"
#include "../world/blockDB.hpp"
#include "../world/structureDB.hpp"
#include "../world/block_interaction.hpp"

std::vector<ConsoleCommand> Console::commands;
std::vector<std::string> Console::logLines;

void Console::init() {
    LOG_INFO("Console: Initializing...");
    if (!commands.empty()) return;

    logLines.push_back("Type 'help' for a list of commands.");

    commands = {
        {
            "say",
            "say <message>",
            "Print a message",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (argString.empty()) {
                    write("Usage: say <message>");
                } else {
                    write(argString);
                }
            }
        },
        {
            "clear",
            "clear",
            "Clear the console window",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                clear();
            }
        },
        {
            "help",
            "help",
            "Show this help page",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                write("Commands:");
                for (const auto& cmd : commands) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "  %s - %s", cmd.usage.c_str(), cmd.description.c_str());
                    write(buf);
                }
            }
        },
        {
            "msg",
            "msg <message>",
            "Show a temporary message above the hotbar",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (argString.empty()) {
                    write("Invalid usage. Use: msg <message>");
                } else {
                    showMessage(argString, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 3.0f);
                }
            }
        },
        {
            "tp",
            "tp <x> <y> <z>",
            "Teleport to coordinates (~ for relative position)",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (args.size() < 3) {
                    write("Invalid usage. Use: tp <x> <y> <z> (~ for current position)");
                    return;
                }
                glm::dvec3 current = camera.getPositionDouble();
                float eyeHeight = camera.getEyeHeight();
                double currentFeetY = current.y - eyeHeight;
                auto parseCoord = [](const std::string& token, double currentVal, bool& ok) -> double {
                    ok = true;
                    if (token[0] == '~') {
                        if (token.size() == 1) return currentVal;
                        try {
                            return currentVal + std::stod(token.substr(1));
                        } catch (...) {
                            ok = false;
                            return 0.0;
                        }
                    }
                    try {
                        return std::stod(token);
                    } catch (...) {
                        ok = false;
                        return 0.0;
                    }
                };
                bool okx = true, oky = true, okz = true;
                double x = parseCoord(args[0], current.x, okx);
                double y = parseCoord(args[1], currentFeetY, oky);
                double z = parseCoord(args[2], current.z, okz);
                if (!okx || !oky || !okz) {
                    write("Invalid coordinates. Example: tp ~ ~10 ~-5");
                } else {
                    double destX = x + (args[0][0] == '~' ? 0.0 : 0.5);
                    double destZ = z + (args[2][0] == '~' ? 0.0 : 0.5);
                    camera.setPosition(glm::dvec3(destX, y + eyeHeight, destZ));
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Teleported to %.2f %.2f %.2f", destX - 0.5, y, destZ - 0.5);
                    write(buf);
                }
            }
        },
        {
            "edgelands",
            "edgelands",
            "Teleport to the edge of the world",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                camera.setPosition(glm::dvec3(2147483635.0, 100.0, 0));
                write("Do not step on blocks right at the edge (game will crash)");
            }
        },
        {
            "give",
            "give <block_id>",
            "Give yourself a block",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (args.empty()) {
                    write("Invalid usage. Use: give <block_id>");
                    return;
                }
                try {
                    int itemId = std::stoi(args[0]);
                    const auto* blockInfo = BlockDB::getBlockInfo(static_cast<uint16_t>(itemId));
                    if (!blockInfo || itemId < 1 || itemId > 65535) {
                        write("Invalid item ID. Block ID must be between 1 and 65535.");
                    } else {
                        setHotbarBlock(selectedHotbarIndex, static_cast<uint16_t>(itemId));
                        setSelectedBlockType(static_cast<uint16_t>(itemId));
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Given: %s (ID: %d)", blockInfo->name.c_str(), itemId);
                        write(buf);
                    }
                } catch (...) {
                    write("Invalid item ID. Must be a number between 1 and 65535.");
                }
            }
        },
        {
            "anchor",
            "anchor",
            "Spawn a block below your feet",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                glm::dvec3 pos = camera.getPositionDouble();
                float eyeHeight = camera.getEyeHeight();
                glm::dvec3 feetPos = glm::dvec3(pos.x, pos.y - eyeHeight, pos.z);

                int blockX = static_cast<int>(std::floor(feetPos.x));
                int blockY = static_cast<int>(std::floor(feetPos.y)) - 1;
                int blockZ = static_cast<int>(std::floor(feetPos.z));

                if (blockY < 0 || blockY >= Chunk::chunkHeight) {
                    write("Cannot place anchor: out of world height bounds.");
                    return;
                }

                int chunkX = worldToChunkCoord(blockX, Chunk::chunkWidth);
                int chunkZ = worldToChunkCoord(blockZ, Chunk::chunkDepth);
                Chunk* chunk = world->getChunk(chunkX, chunkZ);
                if (chunk) {
                    int localX = blockX - chunkX * Chunk::chunkWidth;
                    int localY = blockY;
                    int localZ = blockZ - chunkZ * Chunk::chunkDepth;

                    chunk->blocks[localX][localY][localZ].type = 3; // stone
                    chunk->isModified = true;
                    chunk->buildMesh();

                    if (localX == 0) {
                        Chunk* neighbor = world->getChunk(chunkX - 1, chunkZ);
                        if (neighbor) neighbor->buildMesh();
                    }
                    if (localX == Chunk::chunkWidth - 1) {
                        Chunk* neighbor = world->getChunk(chunkX + 1, chunkZ);
                        if (neighbor) neighbor->buildMesh();
                    }
                    if (localZ == 0) {
                        Chunk* neighbor = world->getChunk(chunkX, chunkZ - 1);
                        if (neighbor) neighbor->buildMesh();
                    }
                    if (localZ == Chunk::chunkDepth - 1) {
                        Chunk* neighbor = world->getChunk(chunkX, chunkZ + 1);
                        if (neighbor) neighbor->buildMesh();
                    }

                    write("Placed stone anchor block.");
                } else {
                    write("Cannot place anchor: chunk not loaded.");
                }
            }
        },
        {
            "structure",
            "structure <x> <y> <z> <structure_name>",
            "Spawn a structure",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (args.size() < 4) {
                    write("Invalid usage. Use: structure <x> <y> <z> <structure_name>");
                    return;
                }
                glm::dvec3 current = camera.getPositionDouble();
                float eyeHeight = camera.getEyeHeight();
                double currentFeetY = current.y - eyeHeight;

                auto parseCoord = [](const std::string& token, double currentVal, bool& ok) -> int {
                    ok = true;
                    if (token[0] == '~') {
                        if (token.size() == 1) return static_cast<int>(std::floor(currentVal));
                        try {
                            return static_cast<int>(std::floor(currentVal + std::stod(token.substr(1))));
                        } catch (...) {
                            ok = false;
                            return 0;
                        }
                    }
                    try {
                        return std::stoi(token);
                    } catch (...) {
                        ok = false;
                        return 0;
                    }
                };

                bool okx = true, oky = true, okz = true;
                int x = parseCoord(args[0], current.x, okx);
                int y = parseCoord(args[1], currentFeetY, oky);
                int z = parseCoord(args[2], current.z, okz);

                if (!okx || !oky || !okz) {
                    write("Invalid coordinates. Example: structure ~ ~ ~ tree");
                    return;
                }

                std::string structName = args[3];
                const Structure* original = StructureDB::get(structName);
                if (!original) {
                    write("Structure not found: " + structName);
                    return;
                }

                if (y < 0 || y >= Chunk::chunkHeight) {
                    write("Cannot spawn structure: out of world height bounds.");
                    return;
                }

                int baseX = x - original->defaultXOffset;
                int baseY = y + original->defaultYOffset;
                int baseZ = z - original->defaultZOffset;

                int chunkX = worldToChunkCoord(baseX, Chunk::chunkWidth);
                int chunkZ = worldToChunkCoord(baseZ, Chunk::chunkDepth);
                Chunk* chunk = world->getChunk(chunkX, chunkZ);
                if (chunk) {
                    int localBaseX = baseX - chunkX * Chunk::chunkWidth;
                    int localBaseZ = baseZ - chunkZ * Chunk::chunkDepth;
                    chunk->placeStructure(*original, localBaseX, baseY, localBaseZ, true);
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Spawned structure '%s' at %d %d %d", structName.c_str(), x, y, z);
                    write(buf);
                } else {
                    write("Cannot spawn structure: chunk not loaded.");
                }
            }
        },
        {
            "fill",
            "fill <fromX> <fromY> <fromZ> <toX> <toY> <toZ> <block_id>",
            "Fill a region with blocks",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (args.size() < 7) {
                    write("Invalid usage. Use: fill <fromX> <fromY> <fromZ> <toX> <toY> <toZ> <block id>");
                    return;
                }
                glm::dvec3 current = camera.getPositionDouble();
                float eyeHeight = camera.getEyeHeight();
                double currentFeetY = current.y - eyeHeight;

                auto parseCoord = [](const std::string& token, double currentVal, bool& ok) -> int {
                    ok = true;
                    if (token[0] == '~') {
                        if (token.size() == 1) return static_cast<int>(std::floor(currentVal));
                        try {
                            return static_cast<int>(std::floor(currentVal + std::stod(token.substr(1))));
                        } catch (...) {
                            ok = false;
                            return 0;
                        }
                    }
                    try {
                        return std::stoi(token);
                    } catch (...) {
                        ok = false;
                        return 0;
                    }
                };

                bool okx1 = true, oky1 = true, okz1 = true;
                bool okx2 = true, oky2 = true, okz2 = true;

                int x1 = parseCoord(args[0], current.x, okx1);
                int y1 = parseCoord(args[1], currentFeetY, oky1);
                int z1 = parseCoord(args[2], current.z, okz1);

                int x2 = parseCoord(args[3], current.x, okx2);
                int y2 = parseCoord(args[4], currentFeetY, oky2);
                int z2 = parseCoord(args[5], current.z, okz2);

                if (!okx1 || !oky1 || !okz1 || !okx2 || !oky2 || !okz2) {
                    write("Invalid coordinates. Example: fill ~ ~ ~ ~5 ~5 ~5 stone");
                    return;
                }

                int minX = std::min(x1, x2);
                int maxX = std::max(x1, x2);
                int minY = std::min(y1, y2);
                int maxY = std::max(y1, y2);
                int minZ = std::min(z1, z2);
                int maxZ = std::max(z1, z2);

                long long volume = static_cast<long long>(maxX - minX + 1) *
                                   static_cast<long long>(maxY - minY + 1) *
                                   static_cast<long long>(maxZ - minZ + 1);
                if (volume > 500000) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Too many blocks in specified area (%lld > 500000)", volume);
                    write(buf);
                    return;
                }

                int startY = std::max(0, minY);
                int endY = std::min(Chunk::chunkHeight - 1, maxY);
                if (startY > endY) {
                    write("Fill area Y coordinate is out of bounds (must be between 0 and 255).");
                    return;
                }

                // Parse block identifier
                std::string blockArg = args[6];
                for (size_t i = 7; i < args.size(); ++i) {
                    blockArg += " " + args[i];
                }

                auto toLower = [](std::string s) -> std::string {
                    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                        return std::tolower(c);
                    });
                    return s;
                };

                std::string blockArgLower = toLower(blockArg);
                uint16_t blockIdVal = 0;
                bool blockFound = false;

                try {
                    size_t idx = 0;
                    int parsedId = std::stoi(blockArg, &idx);
                    if (idx == blockArg.size()) {
                        if (parsedId == 0) {
                            blockIdVal = 0;
                            blockFound = true;
                        } else if (parsedId >= 1 && parsedId <= 65535) {
                            const auto* info = BlockDB::getBlockInfo(static_cast<uint16_t>(parsedId));
                            if (info) {
                                blockIdVal = static_cast<uint16_t>(parsedId);
                                blockFound = true;
                            }
                        }
                    }
                } catch (...) {}

                if (!blockFound) {
                    if (blockArgLower == "air" || blockArgLower == "empty" || blockArgLower == "0") {
                        blockIdVal = 0;
                        blockFound = true;
                    } else {
                        for (uint16_t id : BlockDB::getRegisteredBlockIDs()) {
                            if (id == 0) continue;
                            const auto* info = BlockDB::getBlockInfo(id);
                            if (info) {
                                std::string nameLower = toLower(info->name);
                                std::string nameLowerUnderscore = nameLower;
                                std::replace(nameLowerUnderscore.begin(), nameLowerUnderscore.end(), ' ', '_');

                                std::string nameLowerSpace = nameLower;
                                std::replace(nameLowerSpace.begin(), nameLowerSpace.end(), '_', ' ');

                                if (blockArgLower == nameLower || blockArgLower == nameLowerUnderscore || blockArgLower == nameLowerSpace) {
                                    blockIdVal = id;
                                    blockFound = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!blockFound) {
                    write("Block type not found: " + blockArg);
                    return;
                }

                std::unordered_set<Chunk*> chunksToRebuild;
                int blocksFilled = 0;

                for (int x = minX; x <= maxX; ++x) {
                    for (int z = minZ; z <= maxZ; ++z) {
                        int chunkX = worldToChunkCoord(x, Chunk::chunkWidth);
                        int chunkZ = worldToChunkCoord(z, Chunk::chunkDepth);
                        Chunk* chunk = world->getChunk(chunkX, chunkZ);
                        if (!chunk) continue;

                        int localX = x - chunkX * Chunk::chunkWidth;
                        int localZ = z - chunkZ * Chunk::chunkDepth;

                        bool borderXMin = (localX == 0);
                        bool borderXMax = (localX == Chunk::chunkWidth - 1);
                        bool borderZMin = (localZ == 0);
                        bool borderZMax = (localZ == Chunk::chunkDepth - 1);

                        for (int y = startY; y <= endY; ++y) {
                            if (chunk->blocks[localX][y][localZ].type != blockIdVal) {
                                chunk->blocks[localX][y][localZ].type = blockIdVal;
                                blocksFilled++;
                            }
                        }

                        chunk->isModified = true;
                        chunksToRebuild.insert(chunk);

                        if (borderXMin) {
                            Chunk* n = world->getChunk(chunkX - 1, chunkZ);
                            if (n) chunksToRebuild.insert(n);
                        }
                        if (borderXMax) {
                            Chunk* n = world->getChunk(chunkX + 1, chunkZ);
                            if (n) chunksToRebuild.insert(n);
                        }
                        if (borderZMin) {
                            Chunk* n = world->getChunk(chunkX, chunkZ - 1);
                            if (n) chunksToRebuild.insert(n);
                        }
                        if (borderZMax) {
                            Chunk* n = world->getChunk(chunkX, chunkZ + 1);
                            if (n) chunksToRebuild.insert(n);
                        }
                    }
                }

                for (Chunk* c : chunksToRebuild) {
                    c->buildMesh();
                }

                char buf[128];
                snprintf(buf, sizeof(buf), "Successfully filled %d blocks", blocksFilled);
                write(buf);
            }
        },
        {
            "option",
            "option <name> <value>",
            "Set an option value",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (args.size() < 2) {
                    write("Invalid usage. Use: option <name> <value>");
                    return;
                }
                std::string name = args[0];
                std::string valStr = args[1];
                for (size_t i = 2; i < args.size(); ++i) {
                    valStr += " " + args[i];
                }

                auto isInteger = [](const std::string& s) -> bool {
                    if (s.empty()) return false;
                    size_t start = 0;
                    if (s[0] == '-' || s[0] == '+') {
                        start = 1;
                        if (s.size() == 1) return false;
                    }
                    for (size_t i = start; i < s.size(); ++i) {
                        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
                    }
                    return true;
                };

                auto isFloat = [](const std::string& s) -> bool {
                    if (s.empty()) return false;
                    try {
                        size_t idx = 0;
                        [[unused]] float val = std::stof(s, &idx);
                        return idx == s.size();
                    } catch (...) {
                        return false;
                    }
                };

                if (!optionExists(name)) {
                    if (isInteger(valStr)) {
                        setOption(name, static_cast<float>(std::stoi(valStr)));
                        write("Option '" + name + "' (detected as Integer) set to: " + valStr);
                    } else if (isFloat(valStr)) {
                        setOption(name, std::stof(valStr));
                        write("Option '" + name + "' (detected as Float) set to: " + valStr);
                    } else {
                        setOption(name, valStr);
                        write("Option '" + name + "' (detected as String) set to: " + valStr);
                    }
                } else {
                    std::string currentVal = getOptionString(name, "");
                    if (isInteger(currentVal)) {
                        if (isInteger(valStr)) {
                            setOption(name, static_cast<float>(std::stoi(valStr)));
                            write("Option '" + name + "' set to: " + valStr);
                        } else {
                            write("Error: Option '" + name + "' expects an Integer, but '" + valStr + "' is not a valid integer.");
                            return;
                        }
                    } else if (isFloat(currentVal)) {
                        if (isFloat(valStr)) {
                            setOption(name, std::stof(valStr));
                            write("Option '" + name + "' set to: " + valStr);
                        } else {
                            write("Error: Option '" + name + "' expects a Float, but '" + valStr + "' is not a valid float.");
                            return;
                        }
                    } else {
                        setOption(name, valStr);
                        write("Option '" + name + "' set to: " + valStr);
                    }
                }
                saveOptionsToFile("options.txt");
            }
        },
        {
            "yaw",
            "yaw <value>",
            "Set the camera yaw",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (args.empty()) {
                    write("Invalid usage. Use: yaw <value>");
                    return;
                }
                camera.setRotation(std::stof(args[0]), camera.getPitch());
                write("Yaw set to: " + args[0]);
            }
        },
        {
            "pitch",
            "pitch <value>",
            "Set the camera pitch",
            [](const std::vector<std::string>& args, const std::string& argString, Camera& camera, World* world) {
                if (args.empty()) {
                    write("Invalid usage. Use: pitch <value>");
                    return;
                }
                camera.setRotation(camera.getYaw(), std::stof(args[0]));
                write("Pitch set to: " + args[0]);
            }
        }
    };
    LOG_INFO("Console: Loaded ", commands.size(), " built-in commands");
}

void Console::execute(const std::string& input, Camera& camera, World* world) {
    if (input.empty()) return;

    // Tokenize by whitespace
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(input);
    while (tokenStream >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) return;

    std::string cmdName = tokens[0];
    size_t cmdPos = input.find(cmdName);
    std::string argString;
    
    if (cmdPos != std::string::npos) {
        size_t argPos = cmdPos + cmdName.length();
        // Skip leading whitespace
        while (argPos < input.length() && std::isspace(static_cast<unsigned char>(input[argPos]))) {
            argPos++;
        }
        if (argPos < input.length()) {
            argString = input.substr(argPos);
            LOG_INFO("Console: Player executed command: ", cmdName, " with args: ", argString);
        } else {
            LOG_INFO("Console:Player executed command: ", cmdName);
        }
    }

    std::vector<std::string> args(tokens.begin() + 1, tokens.end());
    bool found = false;
    for (const auto& cmd : commands) {
        if (cmd.name == cmdName) {
            cmd.handler(args, argString, camera, world);
            found = true;
            break;
        }
    }

    if (!found) {
        write("Unknown command. Type 'help' for a list of commands.");
    }
}

void Console::write(const std::string& line) {
    logLines.push_back(line);
}

void Console::clear() {
    logLines.clear();
}

const std::vector<std::string>& Console::getLog() {
    return logLines;
}
