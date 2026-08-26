#include "gtest/gtest.h"

#include "Api/DungeonClearController.h"

TEST(DungeonClearControllerTest, RejectsMissingGroup)
{
    DungeonClear::Controller& controller = DungeonClear::Controller::Instance();

    DungeonClear::Result result = controller.StartForGroup(nullptr);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.error, DungeonClear::Error::InvalidGroup);
    EXPECT_EQ(result.message, "group is not available");
}

TEST(DungeonClearControllerTest, UsesExplicitRequesterAsStartActionOwner)
{
    ObjectGuid const groupLeader = ObjectGuid::Create<HighGuid::Player>(100);
    ObjectGuid const requester = ObjectGuid::Create<HighGuid::Player>(200);
    DungeonClear::StartOptions options;
    options.requesterGuid = requester;

    EXPECT_EQ(options.ResolveOwnerGuid(groupLeader), requester);
}

TEST(DungeonClearControllerTest, FallsBackToGroupLeaderAsStartActionOwner)
{
    ObjectGuid const groupLeader = ObjectGuid::Create<HighGuid::Player>(100);

    EXPECT_EQ(DungeonClear::StartOptions{}.ResolveOwnerGuid(groupLeader), groupLeader);
}

TEST(DungeonClearControllerTest, RejectsFailedStartAction)
{
    DungeonClear::Result result = DungeonClear::Result::FromStartAction(false);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.error, DungeonClear::Error::StartRejected);
    EXPECT_EQ(result.message, "dungeon clear start action was rejected");
}

TEST(DungeonClearControllerTest, RejectsSnapshotForMissingGroup)
{
    DungeonClear::Snapshot snapshot;

    EXPECT_FALSE(DungeonClear::Controller::Instance().GetSnapshot(nullptr, snapshot));
}

struct StopCauseCase
{
    DungeonClear::StopCause cause;
    char const* name;
};

class DungeonClearStopCauseTest : public testing::TestWithParam<StopCauseCase>
{
};

TEST_P(DungeonClearStopCauseTest, HasStableDiagnosticName)
{
    StopCauseCase const& testCase = GetParam();

    EXPECT_STREQ(DungeonClear::StopCauseName(testCase.cause), testCase.name);
}

INSTANTIATE_TEST_SUITE_P(
    StopCauses,
    DungeonClearStopCauseTest,
    testing::Values(
        StopCauseCase{DungeonClear::StopCause::None, "none"},
        StopCauseCase{DungeonClear::StopCause::Manual, "manual"},
        StopCauseCase{DungeonClear::StopCause::Controller, "controller"},
        StopCauseCase{DungeonClear::StopCause::Completed, "completed"},
        StopCauseCase{DungeonClear::StopCause::Wipe, "wipe"},
        StopCauseCase{DungeonClear::StopCause::NoRezzer, "no-rezzer"},
        StopCauseCase{DungeonClear::StopCause::ResurrectionTimeout, "resurrection-timeout"},
        StopCauseCase{DungeonClear::StopCause::LeftInstance, "left-instance"},
        StopCauseCase{DungeonClear::StopCause::Internal, "internal"}));
