#ifndef SnapshotProgressionTests_hpp
#define SnapshotProgressionTests_hpp

#include "FunctionalTestHarness.hpp"

class SnapshotProgressionTests : public FunctionalTestHarness
{
public:
    TEST_CATEGORY(SnapshotProgressionTests);

    TEST(testGetGoalStack, -1)
    void testGetGoalStack();

    TEST(testBasicElaborationAndMatch, -1)
    void testBasicElaborationAndMatch();

    TEST(testInitialState, -1)
    void testInitialState();

    TEST(testString, -1)
    void testString();

    TEST(All_Test_Types, -1)
    void All_Test_Types();

    TEST(Chunk_Superstate_Operator_Preference, -1)
    void Chunk_Superstate_Operator_Preference();

    TEST(Justifications_Get_New_Identities, -1)
    void Justifications_Get_New_Identities();

    TEST(Singletons, -1)
    void Singletons();

    TEST(Operator_Selection_Knowledge_Ghost_Operator, -1)
    void Operator_Selection_Knowledge_Ghost_Operator();

    TEST(All_Test_Types_FinalRoundtrip, -1)
    void All_Test_Types_FinalRoundtrip();

    TEST(All_Test_Types_RoundtripAfterStep1, -1)
    void All_Test_Types_RoundtripAfterStep1();

    TEST(All_Test_Types_RoundtripAfterStep2, -1)
    void All_Test_Types_RoundtripAfterStep2();

    TEST(All_Test_Types_RoundtripAfterStep3, -1)
    void All_Test_Types_RoundtripAfterStep3();

private:
    void runHarnessTest(const std::string& categoryName, const std::string& testName, int expectedDecisions);
    void checkChunk(const char* categoryName, const char* testName, int64_t decisions, int64_t expectedChunks, bool directSourceChunks = false);
    void verifyChunk(const char* categoryName, const char* testName, int64_t expectedChunks, bool directSourceChunks = false);
    void saveChunks(const char* testName, bool directSourceChunks);
    void sourceSavedChunks(const char* testName);
    void runAllTestTypesWithSingleRoundtrip(int roundtripAfterStep);
};

#endif