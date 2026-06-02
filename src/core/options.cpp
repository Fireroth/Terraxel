#include <fstream>
#include <sstream>
#include <map>
#include "options.hpp"
#include "logger.hpp"

static std::map<std::string, std::string> optionsMap;
static bool loaded = false;

void loadOptionsFromFile(const std::string& filename) {
    if (loaded) return;
    std::ifstream file(filename);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ' || val.back() == '\t')) {
                val.pop_back();
            }
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
                key.pop_back();
            }
            optionsMap[key] = val;
            LOG_DEBUG("Options: Loaded option: ", key, " = ", val);
        }
    }
    LOG_INFO("Options: Loaded ", optionsMap.size(), " values from ", filename);
    loaded = true;
}

void setOption(const std::string& key, float value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    optionsMap[key] = buf;
}

void setOption(const std::string& key, const std::string& value) {
    optionsMap[key] = value;
}

void saveOptionsToFile(const std::string& filename) {
    std::ofstream file(filename, std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("Options: Failed to open ", filename, " for writing.");
        return;
    }

    for (const auto& [key, value] : optionsMap) {
        file << key << "=" << value << "\n";
    }
    LOG_INFO("Options: Saved settings to ", filename);
}

int getOptionInt(const std::string& key, const int defaultValue) {
    if (!loaded) loadOptionsFromFile("options.txt");
    auto iterator = optionsMap.find(key);
    if (iterator != optionsMap.end()) {
        try {
            return std::stoi(iterator->second);
        } catch (...) {
            try {
                return static_cast<int>(std::stof(iterator->second));
            } catch (...) {}
        }
    } else {
        LOG_WARN("Options: Option '", key, "' not found, returning and saving default value: ", defaultValue);
        optionsMap[key] = std::to_string(defaultValue);
    }
    return defaultValue;
}

float getOptionFloat(const std::string& key, const float defaultValue) {
    if (!loaded) loadOptionsFromFile("options.txt");
    auto iterator = optionsMap.find(key);
    if (iterator != optionsMap.end()) {
        try {
            return std::stof(iterator->second);
        } catch (...) {}
    } else {
        LOG_WARN("Options: Option '", key, "' not found, returning and saving default value: ", defaultValue);
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", defaultValue);
        optionsMap[key] = buf;
    }
    return defaultValue;
}

std::string getOptionString(const std::string& key, const std::string& defaultValue) {
    if (!loaded) loadOptionsFromFile("options.txt");
    auto iterator = optionsMap.find(key);
    if (iterator != optionsMap.end()) return iterator->second;
    else {
        LOG_WARN("Options: Option '", key, "' not found, returning and saving default value: ", defaultValue);
        optionsMap[key] = defaultValue;
    }
    return defaultValue;
}

bool optionExists(const std::string& key) {
    if (!loaded) loadOptionsFromFile("options.txt");
    return optionsMap.find(key) != optionsMap.end();
}
