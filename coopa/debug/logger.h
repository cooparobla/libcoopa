/**
 * @file logger.h
 * @brief Defines the thread-safe Logger class for writing formatted logs to stderr.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm> // For std::transform
#include <mutex>     // For std::mutex and std::lock_guard

#include <coopa/debug/printer.h>

namespace trav
{
namespace debug
{

/**
 * @class Logger
 * @brief Thread-safe synchronous logger that formats outputs with timestamps and level tags.
 */
class Logger : public Printer {
public:
    /**
     * @brief Constructs a Logger instance.
     * @param name The identification tag for this logger instance (printed in brackets).
     */
    Logger(const std::string& name) : Printer(), name_(name) {}

    /**
     * @brief Virtual destructor.
     */
    virtual ~Logger() = default;

    /**
     * @brief Logs an informational message.
     * @param message Message to log.
     * @param type Category tag (unused by direct Logger).
     * @param is_main_thread Main thread flag (unused by direct Logger).
     * @param indent Indentation level.
     */
    void info(const std::string& message, std::string type = "", bool is_main_thread = false, unsigned int indent = 0) override { 
        log("INFO", message, indent); 
    } 

    /**
     * @brief Logs a warning message.
     * @param message Message to log.
     */
    void warn(const std::string& message) { log("WARN", message); } 

    /**
     * @brief Logs an error message.
     * @param message Message to log.
     */
    void error(const std::string& message) { log("ERROR", message); } 

private:
    /**
     * @brief Formats and writes the log message to stderr.
     * @param level String tag representing the log severity (e.g. "INFO").
     * @param message The content of the log message.
     * @param indent The indentation level for tree formatting.
     */
    void log(std::string level, const std::string& message, unsigned int indent = 0) {
        // Use a lock_guard to ensure the mutex is locked before writing and unlocked afterwards.
        // This provides RAII for mutex locking, ensuring it's always released.
        std::lock_guard<std::mutex> lock(mtx_); 

        auto now = std::chrono::system_clock::now(); 
        
        // Get milliseconds from the time_point
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::time_t time = std::chrono::system_clock::to_time_t(now); 
        std::tm tm_snapshot; 

    #ifdef _WIN32
        // Microsoft Visual C++ specific for thread-safe localtime
        localtime_s(&tm_snapshot, &time); 
    #else
        // POSIX standard for thread-safe localtime
        localtime_r(&time, &tm_snapshot); 
    #endif

        std::stringstream ss; 
        // Format to HH:MM:SS.ms (removed date)
        ss << std::put_time(&tm_snapshot, "%H:%M:%S") << '.' 
           << std::setfill('0') << std::setw(3) << ms.count();
        
        // Construct the full log message and print it as a single operation to minimize
        // potential interleaving if not fully protected by the mutex (though lock_guard
        // should handle the entire block). Using a single cerr << statement improves robustness.
        if (indent > 0) { 
            std::cerr << ss.str() << " [" << level << "]::[\e[3m" << name_ << "\e[0m]" 
                      << std::string(indent, '\t') << "- " << message << std::endl; 
        } else { 
            std::cerr << ss.str() << " [" << level << "]::[\e[3m" << name_ << "\e[0m]" 
                      << " " << message << std::endl; 
        }
    }

    std::string name_; /**< Name of this logger instance. */

    // C++17 inline static member for header-only libraries
    // This allows the mutex to be defined directly in the header
    // and correctly shared across all translation units.
    inline static std::mutex mtx_; /**< Mutex to ensure console printing is thread-safe. */
};

}
}

#endif  // LOGGER_H