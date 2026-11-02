#include <fstream>
#include <map>
#include "options.hpp"
#include "logger.hpp"

static std::map<std::string, std::string> optionsMap;
static bool loaded = false;

static std::map<std::string, std::string> defaultOptions = {
    {"window_width", "1280"},
    {"window_height", "720"},
    {"fullscreen", "0"},
    {"render_distance", "10"},
    {"fov", "70.0"},
    {"vsync", "1"},
    {"fog", "1"},
    {"chunks_to_load_per_frame", "5"},
    {"chunks_to_sort_per_frame", "5"},
    {"hide_console", "1"},
    {"faster_models", "0"},
    {"max_fps", "144"},
    {"show_internal_tab", "0"},
    {"use_mipmaps", "1"},
    {"mipmap_levels", "4"},
    {"lod_bias", "-0.2"},
    {"mouse_sensitivity", "10"},
    {"ambient_occlusion", "1"},
    {"log_level", "INFO"},
    {"enable_lighting", "1"},
    {"vignette", "1"},
    {"clouds", "2"},
    {"cloud_render_distance", "64"},
    {"brightness", "0.1"}
};

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

int getOptionInt(const std::string& key) {
    if (!loaded) loadOptionsFromFile("assets/options.txt");
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
        LOG_WARN("Options: Option '", key, "' not found, returning and saving default value: ", defaultOptions[key]);
        optionsMap[key] = defaultOptions[key];
    }
    return std::stoi(defaultOptions[key]);
}

float getOptionFloat(const std::string& key) {
    if (!loaded) loadOptionsFromFile("assets/options.txt");
    auto iterator = optionsMap.find(key);
    if (iterator != optionsMap.end()) {
        try {
            return std::stof(iterator->second);
        } catch (...) {}
    } else {
        LOG_WARN("Options: Option '", key, "' not found, returning and saving default value: ", defaultOptions[key]);
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", std::stof(defaultOptions[key]));
        optionsMap[key] = buf;
    }
    return std::stof(defaultOptions[key]);
}

std::string getOptionString(const std::string& key, const std::string& defaultValue) {
    if (!loaded) loadOptionsFromFile("assets/options.txt");
    auto iterator = optionsMap.find(key);
    if (iterator != optionsMap.end()) return iterator->second;
    else {
        LOG_WARN("Options: Option '", key, "' not found, returning and saving default value: ", defaultValue);
        optionsMap[key] = defaultValue;
    }
    return defaultValue;
}

bool optionExists(const std::string& key) {
    if (!loaded) loadOptionsFromFile("assets/options.txt");
    return optionsMap.find(key) != optionsMap.end();
}
