#include "ScreenCapture.h"
#include "Common.h"

namespace LeagueRecorder {

    ScreenCapture::ScreenCapture() : m_initialized(false) {
    }

    ScreenCapture::~ScreenCapture() {
        cleanup();
    }

    bool ScreenCapture::initialize() {
        if (m_initialized) {
            return true;
        }

        m_initialized = m_capture.Initialize();

        if (m_initialized) {
            LOGF("[ScreenCapture] Initialized. Screen size: %dx%d",
                m_capture.GetFullWidth(), m_capture.GetFullHeight());
        }
        else {
            LOG("[ScreenCapture] Failed to initialize capture");
        }

        return m_initialized;
    }

    void ScreenCapture::cleanup() {
        if (m_initialized) {
            m_capture.CleanUp();
            m_initialized = false;
        }
    }

    bool ScreenCapture::reinitialize() {
        cleanup();
        return initialize();
    }

    bool ScreenCapture::captureFrame(std::vector<uint8_t>& frameData) {
        if (!m_initialized) {
            return false;
        }

        return m_capture.CaptureScreen(frameData);
    }

    bool ScreenCapture::captureFrameMat(cv::Mat& frame, bool bgr) {
        if (!m_initialized) {
            return false;
        }

        std::vector<uint8_t> frameData;
        if (!m_capture.CaptureScreen(frameData)) {
            return false;
        }

        // Convert to OpenCV Mat (BGRA format from buffer)
        cv::Mat rawFrame(m_capture.GetCaptureHeight(), m_capture.GetCaptureWidth(), CV_8UC4, frameData.data());

        if (bgr) {
            // Convert BGRA to BGR
            cv::cvtColor(rawFrame, frame, cv::COLOR_BGRA2BGR);
        }
        else {
            frame = rawFrame.clone();
        }

        return true;
    }

    bool ScreenCapture::selectCaptureRegion() {
        if (!m_initialized) {
            return false;
        }

        bool success = m_capture.SelectCaptureRegionByMouse();
        if (success) {
            LOGF("[ScreenCapture] New capture region selected: %dx%d",
                m_capture.GetCaptureWidth(), m_capture.GetCaptureHeight());
        }
        return success;
    }

    int ScreenCapture::getCaptureWidth() const {
        return m_initialized ? m_capture.GetCaptureWidth() : 0;
    }

    int ScreenCapture::getCaptureHeight() const {
        return m_initialized ? m_capture.GetCaptureHeight() : 0;
    }

    int ScreenCapture::getFullWidth() const {
        return m_initialized ? m_capture.GetFullWidth() : 0;
    }

    int ScreenCapture::getFullHeight() const {
        return m_initialized ? m_capture.GetFullHeight() : 0;
    }

} // namespace LeagueRecorder