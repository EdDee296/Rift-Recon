#include "VideoProcessor.h"
#include "Common.h"
#include <windows.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace LeagueRecorder {

    VideoProcessor::VideoProcessor(ScreenCapture& capture, ChampionDetector& detector)
        : m_capture(capture)
        , m_detector(detector)
        , m_targetFps(10.0)
        , m_matchThreshold(0.7)
        , m_codec(cv::VideoWriter::fourcc('a', 'v', 'c', '1')) // H.264 codec
        , m_shouldExit(false)
        , m_isRecording(false)
        , m_currentFps(0.0)
        , m_avgFrameTime(0.0)
        , m_maxBufferSize(3)  // 3-frame buffer
        , m_droppedFrames(0)
        , m_useBuffering(true)  // Enable buffering by default
    {
    }

    VideoProcessor::~VideoProcessor() {
        stopRecording("destructor called");
    }

    bool VideoProcessor::startRecording() {
        if (m_isRecording.load()) {
            LOG("[VideoProcessor] Recording already in progress");
            return false;
        }

        // Make sure capture is initialized
        if (m_capture.getCaptureWidth() <= 0 || m_capture.getCaptureHeight() <= 0) {
            LOG("[VideoProcessor] Cannot start recording - capture not initialized");
            return false;
        }

        // Generate a new filename
        m_currentVideoFilename = generateVideoFilename();

        // Initialize video writer
        /*
        m_videoWriter.open(m_currentVideoFilename, m_codec, m_targetFps,
            cv::Size(m_capture.getCaptureWidth(), m_capture.getCaptureHeight()), true);

        if (!m_videoWriter.isOpened()) {
            LOG("[VideoProcessor] Could not open video file for write");
            return false;
        }
        */

        // Clear the frame buffer
        clearFrameBuffer();

        // Reset flags and start threads
        m_shouldExit = false;
        m_isRecording = true;
        m_droppedFrames = 0;

        if (m_useBuffering) {
            // Start both capture and processing threads for buffered mode
            m_captureThread = std::thread(&VideoProcessor::captureThread, this);
            m_processingThread = std::thread(&VideoProcessor::processingThread, this);
            notifyStatus("Processing started (buffered)");
        }
        else {
            // Use original single-threaded approach
            m_recordingThread = std::thread(&VideoProcessor::recordingThread, this);
            notifyStatus("Processing started");
        }

        return true;
    }

    void VideoProcessor::stopRecording(const std::string& reason) {
        // Set flag for threads to exit
        m_shouldExit = true;

        // Notify condition variable to wake up processing thread
        if (m_useBuffering) {
            m_bufferCondition.notify_all();
        }

        // Wait for threads to finish
        if (m_captureThread.joinable()) {
            m_captureThread.join();
        }
        if (m_processingThread.joinable()) {
            m_processingThread.join();
        }
        if (m_recordingThread.joinable()) {
            m_recordingThread.join();
        }

        // Clean up resources if we were recording
        if (m_isRecording.exchange(false)) {
            // Release the video writer
            /*
            if (m_videoWriter.isOpened()) {
                m_videoWriter.release();
            }
            */

            // Clear remaining frames in buffer
            if (m_useBuffering) {
                clearFrameBuffer();
            }

            std::string message = "Processing stopped (" + reason + ")";
            if (m_useBuffering && m_droppedFrames > 0) {
                message += " (Dropped " + std::to_string(m_droppedFrames) + " frames)";
            }
            notifyStatus(message);
        }
    }

    bool VideoProcessor::isRecording() const {
        return m_isRecording.load();
    }

    void VideoProcessor::setStatusCallback(StatusCallback callback) {
        m_statusCallback = callback;
    }

    void VideoProcessor::setTargetFps(double fps) {
        m_targetFps = fps;
    }

    void VideoProcessor::setMatchThreshold(double threshold) {
        m_matchThreshold = threshold;
    }

    void VideoProcessor::setBufferingEnabled(bool enabled) {
        if (m_isRecording.load()) {
            notifyStatus("Cannot change buffering mode while recording");
            return;
        }
        m_useBuffering = enabled;
        notifyStatus(enabled ? "Buffering enabled" : "Buffering disabled");
    }

    void VideoProcessor::setBufferSize(size_t size) {
        if (m_isRecording.load()) {
            notifyStatus("Cannot change buffer size while recording");
            return;
        }
        m_maxBufferSize = (std::max)(size_t(1), (std::min)(size, size_t(10))); // Limit between 1-10
        notifyStatus("Buffer size set to " + std::to_string(m_maxBufferSize));
    }

    bool VideoProcessor::selectCaptureRegion() {
        if (m_isRecording.load()) {
            // Pause recording temporarily
            bool result = m_capture.selectCaptureRegion();
            if (result) {
                // Restart video writer with new dimensions
                if (m_videoWriter.isOpened()) {
                    m_videoWriter.release();
                }

                m_videoWriter.open(m_currentVideoFilename, m_codec, m_targetFps,
                    cv::Size(m_capture.getCaptureWidth(), m_capture.getCaptureHeight()), true);

                if (!m_videoWriter.isOpened()) {
                    notifyStatus("Error: Could not reopen video file after region change");
                    return false;
                }

                std::ostringstream message;
                message << "New capture region selected: "
                    << m_capture.getCaptureWidth() << "x" << m_capture.getCaptureHeight();
                notifyStatus(message.str());
            }
            return result;
        }
        else {
            return m_capture.selectCaptureRegion();
        }
    }

    std::string VideoProcessor::getCurrentVideoFilename() const {
        return m_currentVideoFilename;
    }

    double VideoProcessor::getCurrentFps() const {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        return m_currentFps;
    }

    std::string VideoProcessor::generateVideoFilename() const {
        // Generate a timestamped filename
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);

        // Use the safer localtime_s version
        std::tm timeinfo;
        localtime_s(&timeinfo, &now_time_t);

        std::stringstream ss;
        ss << "game_capture_" << std::put_time(&timeinfo, "%Y%m%d_%H%M%S") << ".mp4";
        return ss.str();
    }

    void VideoProcessor::notifyStatus(const std::string& status) {
        LOG("[VideoProcessor] " + status);
        if (m_statusCallback) {
            m_statusCallback(status);
        }
    }

    void VideoProcessor::clearFrameBuffer() {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        // Clear the queue
        std::queue<cv::Mat> empty;
        m_frameBuffer.swap(empty);
    }

    void VideoProcessor::captureThread() {
        try {
            // For FPS calculation
            auto lastTime = std::chrono::high_resolution_clock::now();
            int fpsCounter = 0;

            const double targetFrameTime = 1000.0 / m_targetFps;
            int failedFrames = 0;
            const int maxFailedFrames = 10;

            // Main capture loop
            while (!m_shouldExit) {
                auto frameStartTime = std::chrono::high_resolution_clock::now();

                // Capture the frame
                cv::Mat bgrFrame;
                if (m_capture.captureFrameMat(bgrFrame, true)) {
                    // Reset failed frame counter on success
                    failedFrames = 0;

                    // Add frame to buffer (non-blocking)
                    {
                        std::lock_guard<std::mutex> lock(m_bufferMutex);

                        // If buffer is full, drop the oldest frame
                        if (m_frameBuffer.size() >= m_maxBufferSize) {
                            m_frameBuffer.pop();
                            m_droppedFrames++;
                        }

                        // Add new frame (move instead of clone to reduce overhead)
                        m_frameBuffer.emplace(std::move(bgrFrame));
                    }

                    // Notify processing thread
                    m_bufferCondition.notify_one();

                    // Update fps counter
                    fpsCounter++;

                    // Calculate FPS
                    auto currentTime = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration<double>(currentTime - lastTime).count();

                    if (elapsed >= 1.0) { // Update FPS every second
                        {
                            std::lock_guard<std::mutex> lock(m_metricsMutex);
                            m_currentFps = fpsCounter / elapsed;
                        }

                        fpsCounter = 0;
                        lastTime = currentTime;

                        // Log capture metrics
                        std::ostringstream statMsg;
                        statMsg << "Capture FPS: " << std::fixed << std::setprecision(1)
                            << (fpsCounter / elapsed);

                        // Show buffer status
                        size_t bufferSize;
                        {
                            std::lock_guard<std::mutex> lock(m_bufferMutex);
                            bufferSize = m_frameBuffer.size();
                        }
                        statMsg << " | Buffer: " << bufferSize << "/" << m_maxBufferSize;

                        if (m_droppedFrames > 0) {
                            statMsg << " | Dropped: " << m_droppedFrames;
                        }

                        notifyStatus(statMsg.str());
                    }
                }
                else {
                    failedFrames++;
                    if (failedFrames >= maxFailedFrames) {
                        notifyStatus("Multiple capture failures. Attempting to reinitialize capture...");
                        if (!m_capture.reinitialize()) {
                            notifyStatus("Reinitialization failed. Consider changing game to borderless mode.");
                        }
                        failedFrames = 0;
                    }

                    // Small sleep to avoid hammering the system during failures
                    Sleep(100);
                }

                // Calculate how long to sleep to maintain target frame rate
                auto frameEndTime = std::chrono::high_resolution_clock::now();
                double frameMs = std::chrono::duration<double, std::milli>(frameEndTime - frameStartTime).count();

                // Only sleep if we're ahead of schedule
                if (frameMs < targetFrameTime) {
                    int sleepTime = static_cast<int>(targetFrameTime - frameMs);
                    if (sleepTime > 0) {
                        Sleep(sleepTime);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            notifyStatus("Exception in capture thread: " + std::string(e.what()));
        }
        catch (...) {
            notifyStatus("Unknown exception in capture thread");
        }
    }

    void VideoProcessor::processingThread() {
        try {
            auto lastFrameTime = std::chrono::high_resolution_clock::now();

            while (!m_shouldExit) {
                cv::Mat frameToProcess;

                // Wait for a frame to be available
                {
                    std::unique_lock<std::mutex> lock(m_bufferMutex);
                    m_bufferCondition.wait(lock, [this] {
                        return !m_frameBuffer.empty() || m_shouldExit;
                        });

                    if (m_shouldExit) {
                        break;
                    }

                    if (!m_frameBuffer.empty()) {
                        frameToProcess = m_frameBuffer.front();
                        m_frameBuffer.pop();
                    }
                }

                if (!frameToProcess.empty()) {
                    auto processingStartTime = std::chrono::high_resolution_clock::now();

                    // Run template matching to detect champions
                    cv::Mat processedFrame = m_detector.detectChampionsInFrame(frameToProcess, m_matchThreshold);

                    // Write the processed frame to video file
                    // m_videoWriter.write(processedFrame);

                    auto processingEndTime = std::chrono::high_resolution_clock::now();
                    double processingMs = std::chrono::duration<double, std::milli>(processingEndTime - processingStartTime).count();

                    // Calculate frame time in milliseconds
                    double frameTime = std::chrono::duration<double, std::milli>(processingEndTime - lastFrameTime).count();
                    lastFrameTime = processingEndTime;

                    // Exponential moving average for frame time (smoothing factor 0.1)
                    {
                        std::lock_guard<std::mutex> lock(m_metricsMutex);
                        m_avgFrameTime = m_avgFrameTime == 0.0 ? frameTime : m_avgFrameTime * 0.9 + frameTime * 0.1;
                    }
                }
            }
        }
        catch (const std::exception& e) {
            notifyStatus("Exception in processing thread: " + std::string(e.what()));
        }
        catch (...) {
            notifyStatus("Unknown exception in processing thread");
        }

        // Clean up at the end
        /*
        if (m_videoWriter.isOpened()) {
            m_videoWriter.release();
        }
        */
    }

    void VideoProcessor::recordingThread() {
        try {
            // For FPS calculation
            auto lastTime = std::chrono::high_resolution_clock::now();
            auto lastFrameTime = lastTime;
            int fpsCounter = 0;

            const double targetFrameTime = 1000.0 / m_targetFps;
            int failedFrames = 0;
            const int maxFailedFrames = 10;

            // Main recording loop (original implementation)
            while (!m_shouldExit) {
                auto frameStartTime = std::chrono::high_resolution_clock::now();

                // Capture and process the frame
                cv::Mat bgrFrame;
                if (m_capture.captureFrameMat(bgrFrame, true)) {
                    // Reset failed frame counter on success
                    failedFrames = 0;
                    auto captureEndTime = std::chrono::high_resolution_clock::now();
                    double captureMs = std::chrono::duration<double, std::milli>(captureEndTime - frameStartTime).count();

                    // Run template matching to detect champions
                    cv::Mat processedFrame = m_detector.detectChampionsInFrame(bgrFrame, m_matchThreshold);

                    // Write the processed frame to video file
                    // m_videoWriter.write(processedFrame);

                    auto processingEndTime = std::chrono::high_resolution_clock::now();
                    double processingMs = std::chrono::duration<double, std::milli>(processingEndTime - captureEndTime).count();

                    // Update fps counter
                    fpsCounter++;

                    // Calculate and display FPS and timing
                    auto currentTime = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration<double>(currentTime - lastTime).count();

                    // Calculate frame time in milliseconds
                    double frameTime = std::chrono::duration<double, std::milli>(currentTime - lastFrameTime).count();
                    lastFrameTime = currentTime;

                    // Exponential moving average for frame time (smoothing factor 0.1)
                    {
                        std::lock_guard<std::mutex> lock(m_metricsMutex);
                        m_avgFrameTime = m_avgFrameTime == 0.0 ? frameTime : m_avgFrameTime * 0.9 + frameTime * 0.1;
                    }

                    if (elapsed >= 1.0) { // Update FPS every second
                        {
                            std::lock_guard<std::mutex> lock(m_metricsMutex);
                            m_currentFps = fpsCounter / elapsed;
                        }

                        fpsCounter = 0;
                        lastTime = currentTime;

                        // Log performance metrics
                        std::ostringstream statMsg;
                        statMsg << "FPS: " << std::fixed << std::setprecision(1) << m_currentFps
                            << " (capture: " << std::setprecision(1) << captureMs << "ms"
                            << ", process: " << std::setprecision(1) << processingMs << "ms"
                            << ", avg: " << std::setprecision(1) << m_avgFrameTime << "ms)";
                        notifyStatus(statMsg.str());
                    }
                }
                else {
                    failedFrames++;
                    if (failedFrames >= maxFailedFrames) {
                        notifyStatus("Multiple capture failures. Attempting to reinitialize capture...");
                        if (!m_capture.reinitialize()) {
                            notifyStatus("Reinitialization failed. Consider changing game to borderless mode.");
                        }
                        failedFrames = 0;
                    }

                    // Small sleep to avoid hammering the system during failures
                    Sleep(100);
                }

                // Calculate how long to sleep to maintain target frame rate
                auto frameEndTime = std::chrono::high_resolution_clock::now();
                double frameMs = std::chrono::duration<double, std::milli>(frameEndTime - frameStartTime).count();

                // Only sleep if we're ahead of schedule
                if (frameMs < targetFrameTime) {
                    int sleepTime = static_cast<int>(targetFrameTime - frameMs);
                    if (sleepTime > 0) {
                        Sleep(sleepTime);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            notifyStatus("Exception in recording thread: " + std::string(e.what()));
        }
        catch (...) {
            notifyStatus("Unknown exception in recording thread");
        }

        // Clean up at the end
        /*
        if (m_videoWriter.isOpened()) {
            m_videoWriter.release();
        }
        */
    }

} // namespace LeagueRecorder