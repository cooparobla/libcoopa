/**
 * @file manager.h
 * @brief Defines the DebugManager class for logging diagnostics and managing the log queue.
 */

#ifndef DEBUG_MANAGER_H
#define DEBUG_MANAGER_H

#include <coopa/job/collections/queue.h>
#include <coopa/debug/message.h>
#include <coopa/debug/context.h>

#include <chrono>
#include <vector>
#include <string>
#include <algorithm> // For std::sort
#include <iostream>
#include <iomanip>   // For std::put_time and std::setfill, std::setw
#include <sstream>   // For std::stringstream

namespace trav {
namespace debug {

/**
 * @class DebugManager
 * @brief Manager class that collects, sorts, and prints queue-based diagnostic logs.
 */
class DebugManager {
public:
    /**
     * @brief Constructs a DebugManager.
     */
    DebugManager() = default; 

    /**
     * @brief Submits a log message into the queue with the current timestamp.
     * @param type_str Log category.
     * @param message Log message text.
     */
    void info(const std::string& type_str, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        log_queue_.push(LogMessage(now, type_str, message));
    }

    /**
     * @brief Flushes all collected log messages, sorts them chronologically, and prints to console.
     */
    void show() {
        std::vector<LogMessage> messages;
        log_queue_.pop_all(messages);

        // Sort messages by timestamp
        // std::chrono::system_clock::time_point inherently supports high precision for sorting.
        std::sort(messages.begin(), messages.end());

        // Print sorted messages
        for (const auto& log_msg : messages) {
            // Get milliseconds from the time_point
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(log_msg.timestamp.time_since_epoch()) % 1000;
            
            // Convert time_point to std::time_t for formatting
            std::time_t time = std::chrono::system_clock::to_time_t(log_msg.timestamp);
            std::tm tm_snapshot;

        #ifdef _WIN32
            localtime_s(&tm_snapshot, &time);
        #else
            localtime_r(&time, &tm_snapshot);
        #endif

            std::stringstream ss;
            // Format to HH:MM:SS.ms (removed date)
            ss << std::put_time(&tm_snapshot, "%H:%M:%S") << '.'
               << std::setfill('0') << std::setw(3) << ms.count();
            
            // Format output similar to trav::debug::Logger class
            std::string tag = " [INFO]::[JOBS]::(parallel)::[\e[3m";
            if (log_msg.is_main_thread) {
                tag = " [INFO]::[JOBS]::(main)::[\e[3m";
            } 
            std::cout << ss.str() << tag << log_msg.type << "\e[0m]"
                      << " " << log_msg.message << std::endl;
        }
    }

    /**
     * @brief Returns a reference to the internal DebugContext.
     * @return Reference to DebugContext.
     */
    DebugContext& get_context() {
        return context_;
    }

private:
    trav::job::ParallelQueue<LogMessage> log_queue_; /**< Thread-safe queue of messages. */
    DebugContext context_ = DebugContext(log_queue_); /**< Associated log context interface. */
};

}
}

#endif // DEBUG_MANAGER_H