#pragma once

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <opencv2/tracking/tracking.hpp> 

namespace LeagueRecorder {

    // Define a structure to store processed template data
    struct ProcessedTemplate {
        cv::Mat original;     // Original template
        cv::Mat bgr;          // BGR version (resized to 30x30)
        cv::Mat bgra;         // BGRA version if needed (resized to 28x28)
        cv::Mat hsv;          // HSV version for histogram
        cv::Mat maskBGR;      // Circular mask for BGR template
        cv::Mat maskBGRA;     // Circular mask for BGRA template
        cv::Mat normalizedHistogram; // Pre-computed normalized histogram
    };

    struct TrackedChampion {
        std::string name;           // Champion name
        cv::Rect boundingBox;     // Current bounding box
        cv::Ptr<cv::TrackerKCF> tracker; // KCF tracker
        int framesTracked;          // Number of frames tracked
        int framesMissed;           // Number of frames missed (for tracking failure handling)
        double lastConfidence;      // Last detection confidence
        bool isTracking;            // Is currently being tracked
        std::string lastKnownPosition; // Last known position (e.g., "top", "mid", "bot", "jungle")

        TrackedChampion(const std::string& championName, const cv::Rect& initialBB, double confidence, const std::string& pos = "")
            : name(championName)
            , boundingBox(initialBB)
            , tracker(cv::TrackerKCF::create())
            , framesTracked(0)
            , framesMissed(0)
            , lastConfidence(confidence)
            , isTracking(false)
            , lastKnownPosition(pos) {
        }
    };

    // Logger utility class for thread-safe console output
    class Logger {
    public:
        static Logger& getInstance() {
            static Logger instance;
            return instance;
        }

        void log(const std::string& message) {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::cout << message << std::endl;
        }

        template<typename... Args>
        void logf(const char* format, Args... args) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), format, args...);
            log(std::string(buffer));
        }

    private:
        Logger() = default;
        std::mutex m_mutex;
    };

    // Utility functions
    inline std::string formatChampionNameForFile(std::string s) {
        // 1) lowercase everything
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::tolower(c); });

        // 2) remove any char that's not a letter or digit
        s.erase(std::remove_if(s.begin(), s.end(),
            [](unsigned char c) {
                return !std::isalnum(c);
            }), s.end());

        return s;
    }

#define LOG(message) LeagueRecorder::Logger::getInstance().log(message)
#define LOGF(...) LeagueRecorder::Logger::getInstance().logf(__VA_ARGS__)

} // namespace LeagueRecorder