#include "pch.h"
#include "../Rift Recon - Core/ChampionDetector.h"
#include "../Rift Recon - Core/Common.h"
#include <filesystem>
#include <opencv2/opencv.hpp>


using namespace LeagueRecorder;

class ChampionDetectorTestable : public LeagueRecorder::ChampionDetector {
public:
    // Expose the asset directory for setting in tests
    void SetAssetDir(const std::string& path) {
        m_assetDir = path;
    }

    // Expose the protected method for direct testing
    std::string CallDetermineMapPosition(double normX, double normY) {
        return determineMapPosition(normX, normY);
    }

    // Expose another protected method
    void CallProcessTemplateOnce(const std::string& name, const cv::Mat& tpl) {
        processTemplateOnce(name, tpl);
    }

    // Expose another protected method
    void CallSendChampionStatusUpdate(const std::string& name, const std::string& status, const std::string& pos) {
        sendChampionStatusUpdate(name, status, pos);
    }

    // Allow tests to look at the processed templates map
    const auto& GetProcessedTemplates() const {
        return m_processedTemplates;
    }

    // Allow tests to look at the last sent message times
    const auto& GetLastMessageSentMap() const {
        return m_lastMessageSent;
    }

    // Allow tests to access the minimap size
    const cv::Size& GetMinimapSize() const {
        return m_minimapSize;
    }
};

// Simple dependencies tests
// A Test Fixture for tests that need a temporary file system
class ChampionDetectorFileTest : public ::testing::Test {
protected:
    // This runs before each test in this fixture
    void SetUp() override {
        // Create a unique temporary directory for our test assets
        tempAssetDir = std::filesystem::temp_directory_path() / "gtest_assets";
        std::filesystem::create_directories(tempAssetDir);

        // Set the detector to use this temporary directory
        detector.SetAssetDir(tempAssetDir.string() + "/");
    }

    // This runs after each test in this fixture
    void TearDown() override {
        // Clean up the directory and its contents
        std::filesystem::remove_all(tempAssetDir);
    }

    // Helper to create a dummy image file for tests
    void CreateDummyChampionImage(const std::string& championName) {
        cv::Mat dummyImage(30, 30, CV_8UC3, cv::Scalar(0, 0, 255)); // A red square
        std::string path = (tempAssetDir / (championName + ".png")).string();
        cv::imwrite(path, dummyImage);
    }

    ChampionDetectorTestable detector;
    std::filesystem::path tempAssetDir;
};

TEST_F(ChampionDetectorFileTest, Initialize_LoadsExistingTemplatesAndIgnoresMissingOnes) {
    // Arrange: Create a dummy file for a champion that exists
    CreateDummyChampionImage("Ahri");
    // We will ask for "Ahri" (exists) and "Zed" (does not exist)
    std::vector<std::string> championsToLoad = { "Ahri", "Zed" };

    // Act: Call the public initialize method
    detector.initialize(championsToLoad);

    // Assert: Check the public state of the detector
    auto loadedNames = detector.getLoadedChampionNames();

    EXPECT_EQ(loadedNames.size(), 1);
    EXPECT_EQ(loadedNames[0], "Ahri");
    // areAllTemplatesLoaded should be true because at least one loaded
    EXPECT_TRUE(detector.areAllTemplatesLoaded());
}


// Pure functional tests
TEST(ChampionDetectorPositionTest, DetermineMapPosition_DetectsCorrectPositions) {
    ChampionDetectorTestable detector;
    // Test cases with expected positions
    struct TestCase {
        double x;
        double y;
        std::string expected;
    };

    std::vector<TestCase> testCases = {
    {0.05, 0.95, "Left Base"},     // close to (0,1)
    {0.95, 0.05, "Right Base"},    // close to (1,0)
    {0.5, 0.55, "Mid Lane"},       // on mid lane (y = -x + 1.05)
    {0.13, 0.13, "Top Lane"},      // near topXOffset/topYOffset
    {0.905, 0.905, "Bot Lane"},    // near botXOffset/botYOffset
    {0.35, 0.35, "Top River"},     // above mid line, within river band
    {0.65, 0.65, "Bot River"}      // below mid line, within river band
    };


    for (const auto& test : testCases) {
        // Call our testable version of the protected method
        std::string result = detector.CallDetermineMapPosition(test.x, test.y);
        EXPECT_EQ(result, test.expected)
            << "Failed at position (" << test.x << ", " << test.y
            << "), expected: " << test.expected << ", got: " << result;
    }
}

TEST(ChampionDetectorPositionTest, ClassifyPosition_IntegratesCorrectly) {
    ChampionDetectorTestable detector;
    // Set the minimap size for classification
    cv::Size m_minimapSize = cv::Size(376, 381);

    // Test various bounding boxes
    struct TestCase {
        cv::Rect boundingBox;
        std::string expectedPosition;
    };

    std::vector<TestCase> testCases = {
        // Left Base: norm = (0.05, 0.95) → center = (18.8, 362.0) → bbox = (9, 352, 20, 20)
        {cv::Rect(9, 352, 20, 20), "Left Base"},

        // Right Base: norm = (0.95, 0.05) → center = (357.2, 19.0) → bbox = (347, 9, 20, 20)
        {cv::Rect(347, 9, 20, 20), "Right Base"},

        // Mid Lane: norm = (0.5, 0.55) → center = (188.0, 209.6) → bbox = (178, 199, 20, 20)
        {cv::Rect(178, 199, 20, 20), "Mid Lane"},

        // Top Lane: norm = (0.13, 0.13) → center = (48.9, 49.5) → bbox = (39, 39, 20, 20)
        {cv::Rect(39, 39, 20, 20), "Top Lane"},

        // Bot Lane: norm = (0.905, 0.905) → center = (340.68, 344.805) → bbox = (331, 335, 20, 20)
        {cv::Rect(331, 335, 20, 20), "Bot Lane"}
    };


    for (const auto& test : testCases) {
        std::string result = detector.classifyPosition(test.boundingBox, m_minimapSize);
        EXPECT_EQ(result, test.expectedPosition)
            << "Failed for bounding box at ("
            << test.boundingBox.x << ", " << test.boundingBox.y << ")";
    }
}

TEST(ChampionDetectorPositionTest, ClassifyPosition_HandlesEdgeCases) {
    ChampionDetectorTestable detector;
    cv::Size m_minimapSize = cv::Size(376, 381);

    // Test edge cases and boundary conditions

    // 1. Bounding box exactly at origin (0,0)
    EXPECT_NE(detector.classifyPosition(cv::Rect(0, 0, 10, 10), m_minimapSize), "");

    // 2. Bounding box that extends beyond frame bounds
    // This tests robustness - should not crash or return empty string
    EXPECT_NE(detector.classifyPosition(cv::Rect(500, 500, 20, 20), m_minimapSize), "");

    // 3. Zero-sized bounding box (degenerate case)
    EXPECT_NE(detector.classifyPosition(cv::Rect(250, 250, 0, 0), m_minimapSize), "");

    // 4. Very large bounding box covering most of minimap
    cv::Rect largeBox(100, 100, 300, 300);
    std::string largeBoxResult = detector.classifyPosition(largeBox, m_minimapSize);
    EXPECT_NE(largeBoxResult, "");
}
