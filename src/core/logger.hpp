#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <ctime>

enum class LogLevel { LVL_TRACE = 0, LVL_DEBUG, LVL_INFO, LVL_WARN, LVL_ERROR, LVL_FATAL };

class Logger {
public:
    static Logger& get() {
        static Logger instance;
        return instance;
    }

    void setLevel(LogLevel level) { minLevel = level; }
    void setFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mtx);
        file.open(path, std::ios::app);
    }

    template<typename... Args>
    void log(LogLevel level, const char* file_, int line, Args&&... args) {
        if (level < minLevel) return;
        std::ostringstream ss;
        ss << timestamp() << " " << levelTag(level) << " ";
        (ss << ... << std::forward<Args>(args));
        if (level != LogLevel::LVL_INFO) {
            ss << "  (" << shortPath(file_) << ":" << line << ")";
        }
        flush(level, ss.str());
    }

    void loadLogLevel() {
        std::ifstream cfg("options.txt");
        if (!cfg.is_open()) {
            log(LogLevel::LVL_WARN, __FILE__, __LINE__, "Logger: options.txt not found. Using default LogLevel::LVL_TRACE");
            return;
        }
        std::string line;
        bool keyFound = false;
        while (std::getline(cfg, line)) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ' || val.back() == '\t'))
                    val.pop_back();
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
                    key.pop_back();
                if (key == "log_level") {
                    keyFound = true;
                    if      (val == "TRACE") setLevel(LogLevel::LVL_TRACE);
                    else if (val == "DEBUG") setLevel(LogLevel::LVL_DEBUG);
                    else if (val == "INFO")  setLevel(LogLevel::LVL_INFO);
                    else if (val == "WARN")  setLevel(LogLevel::LVL_WARN);
                    else if (val == "ERROR") setLevel(LogLevel::LVL_ERROR);
                    else if (val == "FATAL") setLevel(LogLevel::LVL_FATAL);
                    else {
                        log(LogLevel::LVL_WARN, __FILE__, __LINE__, "Logger: Invalid log_level '", val, "' in options.txt.");
                    }
                    break;
                }
            }
        }
        if (!keyFound)
            log(LogLevel::LVL_WARN, __FILE__, __LINE__, "Logger: log_level not found in options.txt.");
    }

private:
    Logger() { loadLogLevel(); }
    ~Logger() { if (file.is_open()) file.close(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel minLevel = LogLevel::LVL_TRACE;
    std::ofstream file;
    std::mutex mtx;

    void flush(LogLevel level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        const char* color = "";
        const char* reset = "\033[0m";
        switch (level) {
            case LogLevel::LVL_TRACE: color = "\033[90m"; break;
            case LogLevel::LVL_DEBUG: color = "\033[36m"; break;
            case LogLevel::LVL_INFO:  color = "\033[32m"; break;
            case LogLevel::LVL_WARN:  color = "\033[33m"; break;
            case LogLevel::LVL_ERROR: color = "\033[31m"; break;
            case LogLevel::LVL_FATAL: color = "\033[35m"; break;
        }
        fprintf(stderr, "%s%s%s\n", color, msg.c_str(), reset);
        if (file.is_open()) file << msg << "\n";
    }

    static const char* levelTag(LogLevel l) {
        switch (l) {
            case LogLevel::LVL_TRACE: return "[TRACE]";
            case LogLevel::LVL_DEBUG: return "[DEBUG]";
            case LogLevel::LVL_INFO:  return "[INFO] ";
            case LogLevel::LVL_WARN:  return "[WARN] ";
            case LogLevel::LVL_ERROR: return "[ERROR]";
            case LogLevel::LVL_FATAL: return "[FATAL]";
        }
        return "[?]";
    }

    static std::string timestamp() {
        time_t t = time(nullptr);
        char buf[20];
#ifdef _WIN32
        struct tm tm_info;
        localtime_s(&tm_info, &t);
        strftime(buf, sizeof(buf), "%H:%M:%S", &tm_info);
#else
        strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
#endif
        return buf;
    }

    static const char* shortPath(const char* p) {
        const char* s = p;
        for (const char* c = p; *c; ++c)
            if (*c == '/' || *c == '\\') s = c + 1;
        return s;
    }
};

#define LOG_TRACE(...) Logger::get().log(LogLevel::LVL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) Logger::get().log(LogLevel::LVL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  Logger::get().log(LogLevel::LVL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  Logger::get().log(LogLevel::LVL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger::get().log(LogLevel::LVL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) Logger::get().log(LogLevel::LVL_FATAL, __FILE__, __LINE__, __VA_ARGS__)