#pragma once
#include "ChampionDetector.h"
#include "ScreenCapture.h"
#include <queue>
#include <condition_variable>

namespace LeagueRecorder {
    class GameCaptureEngine {
    public:
        // Callback type for status updates
        using StatusCallback = std::function<void(const std::string&)>;

        GameCaptureEngine(ScreenCapture& capture, ChampionDetector& detector);
        ~GameCaptureEngine();

        // Start/stop recording
        bool startRecording();
        void stopRecording(const std::string& reason = "manual stop");

        // Check if recording is in progress
        bool isRecording() const;

        // Set callbacks
        void setStatusCallback(StatusCallback callback);

        // Set recording parameters
        void setTargetFps(double fps);
        void setMatchThreshold(double threshold);
        void setBufferingEnabled(bool enabled);    // New: enable/disable buffering
        void setBufferSize(size_t size);           // New: set buffer size (1-10 frames)
        void setVideoSavingEnabled(bool enabled);  // New: enable/disable video saving

        // Handle manual capture region selection
        bool selectCaptureRegion();

        // Get information about the current recording
        std::string getCurrentVideoFilename() const;
        double getCurrentFps() const;

    private:
        // Thread functions
        void captureThread();      // New: handles frame capture
        void processingThread();   // New: handles frame processing
        void recordingThread();    // Keep for backward compatibility if needed

        // Generate a unique filename for the video
        std::string generateVideoFilename() const;

        // Frame buffer management
        void clearFrameBuffer();

        // References to other components
        ScreenCapture& m_capture;
        ChampionDetector& m_detector;

        // Video parameters
        double m_targetFps;
        double m_matchThreshold;
        int m_codec;

        // Recording state
        std::atomic<bool> m_shouldExit;
        std::atomic<bool> m_isRecording;
        std::thread m_captureThread;       // New: separate capture thread
        std::thread m_processingThread;    // New: separate processing thread
        std::thread m_recordingThread;     // Keep existing for compatibility

        // Frame buffering
        std::queue<cv::Mat> m_frameBuffer;
        std::mutex m_bufferMutex;
        std::condition_variable m_bufferCondition;
        size_t m_maxBufferSize;
        std::atomic<int> m_droppedFrames;
        bool m_useBuffering;                       // New: toggle buffering on/off
        bool m_videoSavingEnabled;                 // New: toggle video saving on/off

        // Video writer and status
        std::string m_currentVideoFilename;
        cv::VideoWriter m_videoWriter;

        // Performance metrics
        mutable std::mutex m_metricsMutex;
        double m_currentFps;
        double m_avgFrameTime;

        // Callbacks
        StatusCallback m_statusCallback;

        // Helpers
        void notifyStatus(const std::string& status);
    };
} // namespace LeagueRecorder