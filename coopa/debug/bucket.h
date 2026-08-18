/**
 * @file bucket.h
 * @brief Defines the DebugBucket class for collecting and printing logs in bucketed formats.
 */

#ifndef DEBUG_BUCKET_H
#define DEBUG_BUCKET_H

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

namespace coopa {
namespace debug {

/**
 * @class DebugBucket
 * @brief A container bucket to collect parallel logs, sorting them by timestamp before output.
 */
class DebugBucket {
public:
    /**
     * @brief Constructs a DebugBucket.
     */
    DebugBucket() = default; 

    /**
     * @brief Pushes a message into the bucket's queue with the current timestamp.
     * @param type_str Log category tag.
     * @param message Log message.
     */
    void info(const std::string& type_str, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        log_queue_.push(LogMessage(now, type_str, message));
    }

    /**
     * @brief Sorts and prints all queued log messages to console.
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
            
            // Format output similar to coopa::debug::Logger class
            std::cout << ss.str() << " [INFO]::(parallel)::[\e[3m" << log_msg.type << "\e[0m]"
                      << " " << log_msg.message << std::endl;
        }
    }

    /**
     * @brief Retrieves the associated DebugContext.
     * @return Reference to DebugContext.
     */
    DebugContext& get_context() {
        return context_;
    }

private:
    coopa::job::ParallelQueue<LogMessage> log_queue_; /**< Thread-safe queue storing logged messages. */
    DebugContext context_ = DebugContext(log_queue_); /**< Associated log context wrapper. */
};

}
}

#endif // DEBUG_BUCKET_H