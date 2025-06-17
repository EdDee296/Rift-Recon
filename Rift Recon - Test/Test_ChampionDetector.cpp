#include "pch.h"
#include "../RiotWFM/ChampionDetector.h"
#include "../RiotWFM/Common.h"
#include <filesystem>
#include <opencv2/opencv.hpp>


using namespace LeagueRecorder;

// In Test_ChampionDetector.cpp

// This class inherits from ChampionDetector and makes protected members public for testing.
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
};

TEST(ChampionDetectorLogicTest, DetermineMapPosition_CorrectlyClassifiesLocations) {
    // Arrange: Create our testable detector instance.
    ChampionDetectorTestable detector;

    // Act & Assert: Call the method with known coordinates and check the output.

    // Test Bases
    EXPECT_EQ(detector.CallDetermineMapPosition(0.9, 0.1), "Right Base");
    EXPECT_EQ(detector.CallDetermineMapPosition(0.1, 0.9), "Left Base");

    // Test Lanes
    EXPECT_EQ(detector.CallDetermineMapPosition(0.5, 0.55), "Mid Lane"); // y = -1*0.5 + 1.05 = 0.55
    EXPECT_EQ(detector.CallDetermineMapPosition(0.1, 0.13), "Top Lane");
    EXPECT_EQ(detector.CallDetermineMapPosition(0.9, 0.905), "Bot Lane");

    // Test River
    EXPECT_EQ(detector.CallDetermineMapPosition(0.5, 0.485), "Top River"); // y = 0.97*0.5 + 0 = 0.485
    EXPECT_EQ(detector.CallDetermineMapPosition(0.6, 0.582), "Bot River"); // y = 0.97*0.6 + 0 = 0.582

    // Test Jungle
    EXPECT_EQ(detector.CallDetermineMapPosition(0.2, 0.2), "Blue Top");
    EXPECT_EQ(detector.CallDetermineMapPosition(0.8, 0.8), "Red Bot");
}


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