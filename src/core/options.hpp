#pragma once

#include <string>

void loadOptionsFromFile(const std::string& filename);
void setOption(const std::string& key, float value);
void setOption(const std::string& key, const std::string& value);
void saveOptionsToFile(const std::string& filename);
int getOptionInt(const std::string& key, int defaultValue);
float getOptionFloat(const std::string& key, float defaultValue);
std::string getOptionString(const std::string& key, const std::string& defaultValue);
bool optionExists(const std::string& key);