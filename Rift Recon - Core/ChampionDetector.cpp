#include "ChampionDetector.h"
#include "Common.h"
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp> 

namespace LeagueRecorder {

    ChampionDetector::ChampionDetector()
        : m_assetDir("assets/") {
    }

    ChampionDetector::~ChampionDetector() {
        clearTemplates();
    }

    // Initialize the detector with a list of champion names

    void ChampionDetector::initialize(const std::vector<std::string>& championNames) {
        // Lock for thread safety
        std::lock_guard<std::mutex> lock(m_templateMutex);

        // Clear existing templates
        m_championTemplates.clear();
        m_processedTemplates.clear();

        // Reset champion states
        m_championStates.clear();
        for (const auto& championName : championNames) {
            m_championStates[championName] = ChampionState::Unseen;
        }

        // Load templates for each champion
        for (const auto& championName : championNames) {
            if (loadChampionTemplate(championName)) {
                LOGF("[ChampionDetector] Loaded template for: %s", championName.c_str());
            }
            else {
                LOGF("[ChampionDetector] Failed to load template for: %s", championName.c_str());
            }
        }
    }

    void ChampionDetector::clearTemplates() {
        std::lock_guard<std::mutex> lock(m_templateMutex);
        m_championTemplates.clear();
        m_processedTemplates.clear();
        LOG("[ChampionDetector] Cleared all champion templates");
    }

    bool ChampionDetector::loadChampionTemplate(const std::string& championName) {
        // Skip if already loaded
        if (m_championTemplates.find(championName) != m_championTemplates.end()) {
            return true;
        }

        std::string formattedName = formatChampionNameForFile(championName);
        std::string templatePath = m_assetDir + formattedName + ".png";
        std::cout << "[ChampionDetector] Loading template for: " << formattedName << "from the path: " << templatePath << std::endl;

        cv::Mat championTemplate = cv::imread(templatePath, cv::IMREAD_COLOR);
        if (championTemplate.empty()) {
            LOGF("[ChampionDetector] Template not found for: %s (Path: %s)", championName.c_str(), templatePath.c_str());
            return false;
        }

        m_championTemplates[championName] = championTemplate;
        processTemplateOnce(championName, championTemplate);
        return true;
    }

    // Process the template once for reuse

    void ChampionDetector::debugShowResizedTemplates() {
        std::lock_guard lock(m_templateMutex);
        if (m_processedTemplates.empty()) {
            LOG("[ChampionDetector] No processed templates to show");
            return;
        }

        for (auto& [name, processed] : m_processedTemplates) {
            // Build window title with its actual dimensions
            int w = processed.bgr.cols, h = processed.bgr.rows;
            std::ostringstream winTitle;
            winTitle << name << " (" << w << "×" << h << ")";

            // Create an autosized window (1:1 pixel mapping)
            cv::namedWindow(winTitle.str(), cv::WINDOW_AUTOSIZE);
            cv::imshow(winTitle.str(), processed.bgr);
        }

        // Wait until user presses a key before closing
        LOG("[ChampionDetector] Showing all resized templates. Press any key to continue...");
        cv::waitKey(0);

        // Destroy those windows so they don't linger
        for (auto& [name, processed] : m_processedTemplates) {
            int w = processed.bgr.cols, h = processed.bgr.rows;
            std::ostringstream winTitle;
            winTitle << name << " (" << w << "×" << h << ")";
            cv::destroyWindow(winTitle.str());
        }
    }
 
    void ChampionDetector::processTemplateOnce(const std::string& championName, const cv::Mat& originalTemplate) {
        // Skip if template is empty or already processed
        if (originalTemplate.empty() || m_processedTemplates.find(championName) != m_processedTemplates.end()) {
            return;
        }

        ProcessedTemplate processed;
        processed.original = originalTemplate;

        // Create BGR version (resized to 30x30)
        if (originalTemplate.channels() == 4) {
            cv::cvtColor(originalTemplate, processed.bgr, cv::COLOR_BGRA2BGR);
        }
        else {
            processed.bgr = originalTemplate.clone();
        }
        cv::resize(processed.bgr, processed.bgr, { 30, 30 });

        // Create BGRA version if needed
        if (originalTemplate.channels() == 3) {
            cv::cvtColor(originalTemplate, processed.bgra, cv::COLOR_BGR2BGRA);
            cv::resize(processed.bgra, processed.bgra, { 28, 28 });
        }
        else {
            processed.bgra = originalTemplate.clone();
            cv::resize(processed.bgra, processed.bgra, { 28, 28 });
        }

        // Create circular mask for the template (for both BGR and BGRA versions)
        // For BGR (30x30)
        int radiusBGR = processed.bgr.rows / 2;
        cv::Point centerBGR(processed.bgr.cols / 2, processed.bgr.rows / 2);
        processed.maskBGR = cv::Mat::zeros(processed.bgr.size(), CV_8UC1);
        cv::circle(processed.maskBGR, centerBGR, radiusBGR, cv::Scalar(255), -1);

        // For BGRA (28x28)
        int radiusBGRA = processed.bgra.rows / 2;
        cv::Point centerBGRA(processed.bgra.cols / 2, processed.bgra.rows / 2);
        processed.maskBGRA = cv::Mat::zeros(processed.bgra.size(), CV_8UC1);
        cv::circle(processed.maskBGRA, centerBGRA, radiusBGRA, cv::Scalar(255), -1);

        // Apply mask to the templates
        cv::Mat maskedBGR;
        processed.bgr.copyTo(maskedBGR, processed.maskBGR);
        processed.bgr = maskedBGR;

        cv::Mat maskedBGRA;
        processed.bgra.copyTo(maskedBGRA, processed.maskBGRA);
        processed.bgra = maskedBGRA;

        // Pre-compute the HSV version and histogram for the template (now using masked version)
        cv::cvtColor(processed.bgr, processed.hsv, cv::COLOR_BGR2HSV);

        // Parameters for a 1D Hue-only histogram with fewer bins (16 instead of 30)
        int hBins = 16;  // Reduced from 30 to 16
        int histSize[] = { hBins };
        float hRanges[] = { 0, 180 };
        const float* ranges[] = { hRanges };
        int channels[] = { 0 };  // Only use the Hue channel (0)

        // Calculate and normalize the histogram (using the mask)
        cv::calcHist(&processed.hsv, 1, channels, processed.maskBGR, processed.normalizedHistogram,
            1, histSize, ranges, true, false);  // Changed from 2D to 1D histogram
        cv::normalize(processed.normalizedHistogram, processed.normalizedHistogram,
            0, 1, cv::NORM_MINMAX);

        // Store in map
        m_processedTemplates[championName] = processed;
    }

    // Detect and update candidates from the current frame

    std::string ChampionDetector::classifyPosition(const cv::Rect& boundingBox, const cv::Size& frameSize) {
        // Convert bounding box center to normalized coordinates
        cv::Point2f normalizedPos = convertToNormalizedCoords(boundingBox, frameSize);
        return determineMapPosition(normalizedPos.x, normalizedPos.y);
    }

    cv::Point2f ChampionDetector::convertToNormalizedCoords(const cv::Rect& boundingBox, const cv::Size& frameSize) {
        // Get center of bounding box
        float centerX = boundingBox.x + boundingBox.width / 2.0f;
        float centerY = boundingBox.y + boundingBox.height / 2.0f;

        // Normalize to 0-1 range
        float normX = centerX / frameSize.width;
        float normY = centerY / frameSize.height;

        return cv::Point2f(normX, normY);
    }

    std::string ChampionDetector::determineMapPosition(double normX, double normY) {
        const auto& config = m_positionConfig;

        // Check base areas first
        double distLeftBase = std::sqrt((normX - 0) * (normX - 0) + (normY - 1) * (normY - 1));
        double distRightBase = std::sqrt((normX - 1) * (normX - 1) + (normY - 0) * (normY - 0));

        if (distLeftBase <= config.baseRadius) {
            return "Left Base";
        }
        if (distRightBase <= config.baseRadius) {
            return "Right Base";
        }

        // Check Mid Lane
        double midCenterY = config.midSlope * normX + config.midIntercept;
        bool isInMid = (midCenterY - config.midThreshold <= normY &&
            normY <= midCenterY + config.midThreshold);
        if (isInMid) {
            return "Mid Lane";
        }

        // Check Top Lane
        bool isInTopY = (config.topYOffset - config.topYThreshold <= normY &&
            normY <= config.topYOffset + config.topYThreshold);
        bool isInTopX = (config.topXOffset - config.topXThreshold <= normX &&
            normX <= config.topXOffset + config.topXThreshold);
        if (isInTopY || isInTopX) {
            return "Top Lane";
        }

        // Check Bot Lane
        bool isInBotY = (config.botYOffset - config.botYThreshold <= normY &&
            normY <= config.botYOffset + config.botYThreshold);
        bool isInBotX = (config.botXOffset - config.botXThreshold <= normX &&
            normX <= config.botXOffset + config.botXThreshold);
        if (isInBotY || isInBotX) {
            return "Bot Lane";
        }

        // Check River
        double riverCenterY = config.riverSlope * normX + config.riverIntercept;
        bool isInRiverBand = (riverCenterY - config.riverThreshold <= normY &&
            normY <= riverCenterY + config.riverThreshold);
        if (isInRiverBand) {
            double midLineY = config.midSlope * normX + config.midIntercept;
            if (normY < midLineY) {
                return "Top River";
            }
            else {
                return "Bot River";
            }
        }

        // Classify as jungle (catch-all)
        double yOnMidLine = config.midSlope * normX + config.midIntercept;
        double yOnRiverLine = config.riverSlope * normX + config.riverIntercept;

        bool isAboveMidLine = normY < yOnMidLine;
        bool isAboveRiverLine = normY < yOnRiverLine;

        if (isAboveMidLine) {
            if (isAboveRiverLine) {
                return "Red Top";
            }
            else {
                return "Blue Top";
            }
        }
        else {
            if (!isAboveRiverLine) {
                return "Red Bot";
            }
            else {
                return "Blue Bot";
            }
        }
    }

    void ChampionDetector::updateCandidateDetection(const std::string& championName, const cv::Rect& boundingBox, double confidence) {
        std::lock_guard<std::mutex> lock(m_candidateMutex);

        auto now = std::chrono::steady_clock::now();

        // Check if this champion already exists in candidates
        auto it = std::find_if(m_candidateDetections.begin(), m_candidateDetections.end(),
            [&championName](const CandidateDetection& candidate) {
                return candidate.name == championName;
            });

        // If found, update it
        if (it != m_candidateDetections.end()) {
            // Calculate time since last detection
            auto timeSinceLastDetection = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->lastSeen).count();

            // If detection is recent enough, increment consecutive count
            if (timeSinceLastDetection <= MAX_DETECTION_GAP_MS) {
                it->consecutiveDetections++;
                LOGF("[ChampionDetector] Updated candidate %s: %d consecutive detections",
                    championName.c_str(), it->consecutiveDetections);
            }
            else {
                // Too much time has passed, reset to 1
                it->consecutiveDetections = 1;
                LOGF("[ChampionDetector] Reset candidate %s: detection gap too large (%d ms)",
                    championName.c_str(), (int)timeSinceLastDetection);
            }

            // Update other properties
            it->boundingBox = boundingBox;
            it->confidence = confidence;
            it->lastSeen = now;
        }
        // Otherwise create a new candidate
        else {
            m_candidateDetections.emplace_back(championName, boundingBox, confidence);
            LOGF("[ChampionDetector] New candidate %s: detection #1", championName.c_str());
        }
    }

    void ChampionDetector::processCandidates() {
        std::lock_guard<std::mutex> candidateLock(m_candidateMutex);
        std::lock_guard<std::mutex> trackerLock(m_trackerMutex);

        auto now = std::chrono::steady_clock::now();

        // Process each candidate
        auto it = m_candidateDetections.begin();
        while (it != m_candidateDetections.end()) {
            auto& candidate = *it;

            // If we have enough consecutive detections, promote to tracker
            if (candidate.consecutiveDetections >= REQUIRED_CONSECUTIVE_DETECTIONS) {
                std::string position = classifyPosition(candidate.boundingBox, cv::Size(m_minimapSize));

                LOGF("[ChampionDetector] Promoting %s to tracker at %s after %d consecutive detections",
                    candidate.name.c_str(), position.c_str(), candidate.consecutiveDetections);

                // Create tracker with position info
                m_trackedChampions.emplace_back(candidate.name, candidate.boundingBox,
                    candidate.confidence, position);
                m_isChampionTracked[candidate.name] = true;

                // Update state with position
                ChampionState prevState = m_championStates[candidate.name];
                if (prevState == ChampionState::Unseen || prevState == ChampionState::Missing) {
                    m_championStates[candidate.name] = ChampionState::Spotted;
                    m_framesSinceStateChange[candidate.name] = 0;

                    // Send spotted notification with position
                    sendChampionStatusUpdate(candidate.name, "spotted", position);
                }

                it = m_candidateDetections.erase(it);
                continue;
            }

            // Check if this candidate has gone stale (no updates for too long)
            auto timeSinceLastSeen = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - candidate.lastSeen).count();

            if (timeSinceLastSeen > MAX_DETECTION_GAP_MS) {
                LOGF("[ChampionDetector] Removing stale candidate %s: not seen for %d ms",
                    candidate.name.c_str(), (int)timeSinceLastSeen);
                it = m_candidateDetections.erase(it);
                continue;
            }

            ++it;
        }
    }

    cv::Mat ChampionDetector::detectChampionsInFrame(const cv::Mat& frame, double matchThreshold) {
        // Clone the frame for drawing on
        cv::Mat resultImage = frame.clone();

        // Step 1: Update trackers for already detected champions
        {
            std::lock_guard<std::mutex> trackerLock(m_trackerMutex);

            // Initialize tracking state for this frame
            for (const auto& championName : getLoadedChampionNames()) {
                m_isChampionTracked[championName] = false;
            }

            // Process each tracker
            auto it = m_trackedChampions.begin();
            while (it != m_trackedChampions.end()) {
                auto& champion = *it;

                // Mark this champion as being tracked
                m_isChampionTracked[champion.name] = true;

                // If tracker needs to be initialized
                if (!champion.isTracking) {
                    try {
                        champion.tracker->init(frame, champion.boundingBox);
                        champion.isTracking = true;
                    }
                    catch (const cv::Exception& e) {
                        LOGF("[ChampionDetector] Failed to initialize tracker for %s: %s", champion.name.c_str(), e.what());
                        champion.isTracking = false;
                    }
                }
                // Otherwise update existing tracker
                else {
                    // If we've tracked too many frames, reset tracker to prevent drift
                    if (champion.framesTracked >= MAX_TRACKING_FRAMES) {
                        LOGF("[ChampionDetector] Resetting tracker for %s after %d frames",
                            champion.name.c_str(), champion.framesTracked);
                        m_isChampionTracked[champion.name] = false;  // Force re-detection
                        it = m_trackedChampions.erase(it);
                        continue;
                    }

                    // Update the tracker
                    bool trackingSuccess = champion.tracker->update(frame, champion.boundingBox);

                    if (trackingSuccess) {
                        champion.framesTracked++;
                        champion.framesMissed = 0;

                        // Increment frames in spotted state
                        if (m_championStates[champion.name] == ChampionState::Spotted) {
                            m_framesSinceStateChange[champion.name]++;
                        }

                        // Draw the tracked box
                        cv::rectangle(resultImage, champion.boundingBox, { 0, 255, 0 }, 2);
                        std::ostringstream ss;
                        ss << champion.name << " (tracked: " << champion.framesTracked << ")";
                        cv::putText(resultImage, ss.str(),
                            { (int)champion.boundingBox.x, (int)champion.boundingBox.y - 5 },
                            cv::FONT_HERSHEY_SIMPLEX, 0.5, { 0, 255, 0 }, 2);
                    }
                    else {
                        champion.framesMissed++;
                        LOGF("[ChampionDetector] Lost tracking for %s (missed: %d/%d)",
                            champion.name.c_str(), champion.framesMissed, MAX_MISSED_FRAMES);

                        // If too many misses, remove the tracker
                        if (champion.framesMissed >= MAX_MISSED_FRAMES) {
                            // Check for true state transition from spotted to missing
                            ChampionState prevState = m_championStates[champion.name];
                            if (prevState == ChampionState::Spotted) {
                                m_championStates[champion.name] = ChampionState::Missing;
                                m_framesSinceStateChange[champion.name] = 0;

                                // Send missing notification immediately on state transition
                                sendChampionStatusUpdate(champion.name, "missing", champion.lastKnownPosition);
                            }

                            m_isChampionTracked[champion.name] = false;
                            it = m_trackedChampions.erase(it);
                            continue;
                        }
                    }
                }

                ++it;
            }
        }

        // Step 2: Run template matching only for champions not being tracked
        // Lock for thread safety when accessing templates
        std::lock_guard<std::mutex> templateLock(m_templateMutex);

        // Process each champion template that is not already being tracked
        for (const auto& [championName, originalTempl] : m_championTemplates) {
            // Skip if this champion is already being tracked
            if (m_isChampionTracked[championName]) {
                continue;
            }

            if (originalTempl.empty()) continue;

            // Ensure we've processed this template
            if (m_processedTemplates.find(championName) == m_processedTemplates.end())
                processTemplateOnce(championName, originalTempl);

            const auto& processed = m_processedTemplates[championName];
            const cv::Mat& tpl = (frame.channels() == 3) ? processed.bgr : processed.bgra;
            const cv::Mat& mask = (frame.channels() == 3) ? processed.maskBGR : processed.maskBGRA;

            if (tpl.empty()) continue;

            int rw = frame.cols - tpl.cols + 1;
            int rh = frame.rows - tpl.rows + 1;
            if (rw <= 0 || rh <= 0) {
                LOGF("[ChampionDetector] Warning: '%s' template larger than frame", championName.c_str());
                continue;
            }

            // 1) Template matching with mask
            cv::Mat resp(rh, rw, CV_32FC1);
            // Use TM_CCORR_NORMED which supports masks
            cv::matchTemplate(frame, tpl, resp, cv::TM_CCORR_NORMED, mask);
            double minV, maxV; cv::Point minL, maxL;
            cv::minMaxLoc(resp, &minV, &maxV, &minL, &maxL);

            if (maxV < matchThreshold)
                continue;

            // 2) Extract candidate patch
            cv::Rect matchedRegion{ maxL.x, maxL.y, tpl.cols, tpl.rows };
            cv::Mat matchedPatch = frame(matchedRegion);

            // 3) Convert patch to HSV
            cv::Mat matchedHsv;
            cv::cvtColor(matchedPatch, matchedHsv, cv::COLOR_BGR2HSV);

            // 4) Compute its Hue-only histogram (1D) with 16 bins
            int hBins = 16;  // Same as in processTemplateOnce
            int histSize[] = { hBins };
            float hRanges[] = { 0, 180 };
            const float* ranges[] = { hRanges };
            int channels[] = { 0 };  // Only use the Hue channel (0)

            cv::Mat matchedHist;
            // Use the same mask for histogram calculation
            cv::calcHist(&matchedHsv, 1, channels, mask,
                matchedHist, 1, histSize, ranges, true, false);
            cv::normalize(matchedHist, matchedHist, 0, 1, cv::NORM_MINMAX);

            // 5) Compare to template's precomputed histogram using Bhattacharyya distance
            double histDistance = cv::compareHist(processed.normalizedHistogram,
                matchedHist, cv::HISTCMP_BHATTACHARYYA);
            double histMatch = 1.0 - histDistance;

            const double histThreshold = 0.6;  // Adjusted threshold for Bhattacharyya method

            std::cout
                << championName
                << " TM=" << maxV
                << "  HIST=" << histMatch
                << std::endl;

            if (histMatch >= histThreshold) {
                // Calculate combined confidence score
                double confidence = (maxV + histMatch) / 2.0;

                // Get position information
                std::string position = classifyPosition(matchedRegion, frame.size());

                // Add/update to candidate detections with position
                updateCandidateDetection(championName, matchedRegion, confidence);

                // Log detection with position
                LOGF("[ChampionDetector] %s detected at %s - tm=%.2f hist=%.2f",
                    championName.c_str(), position.c_str(), maxV, histMatch);

                // Draw the detection with position info
                {
                    std::lock_guard<std::mutex> candidateLock(m_candidateMutex);
                    auto it = std::find_if(m_candidateDetections.begin(), m_candidateDetections.end(),
                        [&championName](const CandidateDetection& candidate) {
                            return candidate.name == championName;
                        });

                    if (it != m_candidateDetections.end()) {
                        cv::rectangle(resultImage, matchedRegion, { 255, 165, 0 }, 2);

                        std::ostringstream ss;
                        ss << championName << " @ " << position
                            << " (" << it->consecutiveDetections << "/" << REQUIRED_CONSECUTIVE_DETECTIONS << ")";

                        cv::putText(resultImage, ss.str(),
                            { matchedRegion.x, matchedRegion.y - 5 },
                            cv::FONT_HERSHEY_SIMPLEX, 0.4, { 255, 165, 0 }, 2);
                    }
                }
            }
        }

        // Process candidates to see if any should be promoted to tracked champions
        processCandidates();

        // Update frame counters for tracked and missing champions
        for (auto& [name, state] : m_championStates) {
            m_framesSinceStateChange[name]++;
        }

        // Display notifications based on champion states (visual only)
        // 1) Spotted champions
        for (auto& champ : m_trackedChampions) {
            if (m_championStates[champ.name] == ChampionState::Spotted) {
                // Check if this is a recently spotted champion (within 30 frames)
                if (m_framesSinceStateChange[champ.name] < 30) {
                    // Show spotted notification on screen
                    cv::putText(resultImage,
                        champ.name + " spotted!",
                        { 10, 30 },                       // draw in top-left corner
                        cv::FONT_HERSHEY_SIMPLEX,
                        1.0,                              // font scale
                        cv::Scalar(0, 0, 255),           // red
                        2);
                }
            }
        }

        // 2) Missing champions
        for (auto& [name, state] : m_championStates) {
            if (state == ChampionState::Missing) {
                // Check if this is a recently missing champion (within 30 frames)
                if (m_framesSinceStateChange[name] < 30) {
                    // Show missing notification on screen
                    cv::putText(resultImage,
                        name + " missing!",
                        { 10, resultImage.rows - 30 },   // bottom-left corner
                        cv::FONT_HERSHEY_SIMPLEX,
                        1.0,
                        cv::Scalar(0, 0, 255),
                        2);
                }
            }
        }

        return resultImage;
    }

    // State and status management

    void ChampionDetector::setWebSocketServer(WebSocketServer* server) {
        m_webSocketServer = server;
    }

    void ChampionDetector::sendChampionStatusUpdate(const std::string& championName, const std::string& status, const std::string& position) {
        if (!m_webSocketServer) return;

        try {
            // Get current timestamp for deduplication
            auto now = std::chrono::system_clock::now();
            auto timestamp = now.time_since_epoch().count();

            // Check if we've sent this exact message recently
            std::string messageKey = championName + ":" + status;
            auto it = m_lastMessageSent.find(messageKey);

            if (it != m_lastMessageSent.end()) {
                // If the same message was sent less than 1 second ago, skip
                auto timeSinceLastMessage = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second).count();

                if (timeSinceLastMessage < 1000) {
                    LOGF("[ChampionDetector] Skipping duplicate message for %s: %s (sent %dms ago)",
                        championName.c_str(), status.c_str(), (int)timeSinceLastMessage);
                    return;
                }
            }

            // Record that we're sending this message now
            m_lastMessageSent[messageKey] = now;

            // Create and send the message
            nlohmann::json message = {
                {"event", "champion_status"},
                {"champion", championName},
                {"status", status},
                {"timestamp", timestamp}
            };

            // Add position if provided
            if (!position.empty()) {
                message["position"] = position;
            }

            m_webSocketServer->broadcastMessage(message.dump());
            LOGF("[ChampionDetector] Sent status update: %s is %s", championName.c_str(), status.c_str());
        }
        catch (const std::exception& e) {
            LOGF("[ChampionDetector] Error sending WebSocket message: %s", e.what());
        }
    }

    bool ChampionDetector::areAllTemplatesLoaded() const {
        std::lock_guard<std::mutex> lock(m_templateMutex);
        return !m_championTemplates.empty();
    }

    std::vector<std::string> ChampionDetector::getLoadedChampionNames() const {
        std::lock_guard<std::mutex> lock(m_templateMutex);
        std::vector<std::string> names;
        for (const auto& [name, _] : m_championTemplates) {
            names.push_back(name);
        }
        return names;
    }

} // namespace LeagueRecorder