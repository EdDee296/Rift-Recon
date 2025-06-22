#pragma once

#include "Common.h"
#include "wsk.h"

class ChampionDetectorTest;

namespace LeagueRecorder {

    // Structure to track candidate detections before they become tracked champions
    struct CandidateDetection {
        std::string name;          // Champion name
        cv::Rect boundingBox;      // Bounding box
        double confidence;         // Detection confidence
        int consecutiveDetections; // Number of consecutive frames detected
        std::chrono::steady_clock::time_point lastSeen; // When it was last seen

        CandidateDetection(const std::string& championName, const cv::Rect& bb, double conf)
            : name(championName)
            , boundingBox(bb)
            , confidence(conf)
            , consecutiveDetections(1)
            , lastSeen(std::chrono::steady_clock::now()) {
        }
    };

    struct PositionConfig {
        // Mid Lane (Diagonal: y = slope*x + intercept)
        double midThreshold = 0.05;
        double midSlope = -1.0;
        double midIntercept = 1.05;

        // Top Lane Horizontal/Vertical
        double topYOffset = 0.13;
        double topYThreshold = 0.03;
        double topXOffset = 0.13;
        double topXThreshold = 0.03;

        // Bot Lane Horizontal/Vertical
        double botYOffset = 0.905;
        double botYThreshold = 0.03;
        double botXOffset = 0.905;
        double botXThreshold = 0.03;

        // River (Diagonal: y = slope*x + intercept)
        double riverThreshold = 0.07;
        double riverSlope = 0.97;
        double riverIntercept = 0.00;

        // Base radius
        double baseRadius = 0.4;
    };

    class ChampionDetector {
    public:
        ChampionDetector();
        ~ChampionDetector();

        // Initialize with a list of champion names
        void initialize(const std::vector<std::string>& championNames);

        void debugShowResizedTemplates();

		// Convert position to minimap coordinates
        std::string classifyPosition(const cv::Rect& boundingBox, const cv::Size& frameSize);

        // Clear all templates from memory
        void clearTemplates();

        // Process a frame and detect champions
        cv::Mat detectChampionsInFrame(const cv::Mat& frame, double matchThreshold);

        // Check if all templates are loaded successfully
        bool areAllTemplatesLoaded() const;

        // Get information about loaded templates
        std::vector<std::string> getLoadedChampionNames() const;

        // WebSocket integration
        void setWebSocketServer(WebSocketServer* server);

        void setMinimapSize(const cv::Size& size) { m_minimapSize = size; }
        void updatePositionConfig(const PositionConfig& config) { m_positionConfig = config; }

    protected:
        friend class ::ChampionDetectorTest;

        enum class ChampionState {
            Unseen,    // Never detected
            Spotted,   // Currently detected
            Missing    // Was spotted but now missing
        };
        
        PositionConfig m_positionConfig;
        cv::Size m_minimapSize{ 376, 381 }; // Default minimap size, update based on your actual minimap

        // Map to track recently sent messages to avoid duplicates
        std::unordered_map<std::string, std::chrono::system_clock::time_point> m_lastMessageSent;

        // Load a champion template
        bool loadChampionTemplate(const std::string& championName);

        // Process a template once for reuse
        void processTemplateOnce(const std::string& championName, const cv::Mat& originalTemplate);

		// Convert a bounding box to normalized coordinates (0-1 range) then to minimap position
        cv::Point2f convertToNormalizedCoords(const cv::Rect& boundingBox, const cv::Size& frameSize);
        std::string determineMapPosition(double normX, double normY);

        // Update candidates from detections
        void updateCandidateDetection(const std::string& championName, const cv::Rect& boundingBox, double confidence);

        // Check and promote candidates to trackers if they meet the criteria
        void processCandidates();

        // when a champion was first promoted to tracking
        std::map<std::string, std::chrono::steady_clock::time_point> m_trackingStart;

        // when a champion was dropped (last seen)
        std::map<std::string, std::chrono::steady_clock::time_point> m_lostStart;

        // to avoid spamming the text every frame
        std::map<std::string, bool> m_shownSpotted;
        std::map<std::string, bool> m_shownMissing;

        // Tracked champions
        std::vector<TrackedChampion> m_trackedChampions;
        std::mutex m_trackerMutex;

        // Track the current state of each champion
        std::map<std::string, ChampionState> m_championStates;

        // Candidate detections (before becoming tracked)
        std::vector<CandidateDetection> m_candidateDetections;
        std::mutex m_candidateMutex;

        // Track frames since state change for each champion
        std::map<std::string, int> m_framesSinceStateChange;

        // How many frames to track before re-detecting (to prevent drift)
        const int MAX_TRACKING_FRAMES = 30;

        // How many missed frames before we consider tracking lost
        const int MAX_MISSED_FRAMES = 5;

        // How many consecutive detections needed before starting to track
        const int REQUIRED_CONSECUTIVE_DETECTIONS = 1;

        // Maximum time between consecutive detections (in milliseconds)
        const int MAX_DETECTION_GAP_MS = 500;

        // Which champions are already being tracked
        std::map<std::string, bool> m_isChampionTracked;

        // Templates and processed templates
        mutable std::mutex m_templateMutex;
        std::map<std::string, cv::Mat> m_championTemplates;
        std::map<std::string, ProcessedTemplate> m_processedTemplates;

        // Asset directory
        std::string m_assetDir;

        // WebSocket server reference for notifications (not owned by this class)
        WebSocketServer* m_webSocketServer = nullptr;

        // Send champion status updates over WebSocket
        void sendChampionStatusUpdate(const std::string& championName, const std::string& status, const std::string& position = "");

    };

} // namespace LeagueRecorder