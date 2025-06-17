#pragma once

#include "DXGIFullscreenCapture.h"
#include <vector>
#include <cstdint>
#include <opencv2/opencv.hpp>

namespace LeagueRecorder {

    class ScreenCapture {
    public:
        ScreenCapture();
        ~ScreenCapture();

        // Initialize the capture
        bool initialize();

        // Clean up resources
        void cleanup();

        // Reinitialize if needed
        bool reinitialize();

        // Capture a frame
        bool captureFrame(std::vector<uint8_t>& frameData);

        // Capture a frame and convert to OpenCV Mat
        bool captureFrameMat(cv::Mat& frameData, bool bgr = true);

        // Select a region of the screen to capture
        bool selectCaptureRegion();

        // Get capture dimensions
        int getCaptureWidth() const;
        int getCaptureHeight() const;

        // Get full screen dimensions
        int getFullWidth() const;
        int getFullHeight() const;

    private:
        DXGIFullscreenCapture m_capture;
        bool m_initialized;
    };

} // namespace LeagueRecorder