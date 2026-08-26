/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "gtest/gtest.h"

#include <cstdint>

#include "TestRun/DcRaidRosterReservation.h"

using DcRaidRosterReservation::Error;
using DcRaidRosterReservation::Store;
using DcRaidLaunchRequest::Member;
using DcRaidLaunchRequest::Request;
using DcRaidLaunchRequest::Role;

namespace
{
    Request Roster(std::uint64_t first = 1)
    {
        Request request;
        request.members = {
            {first, "Tank", Role::MainTank, 1},
            {first + 1, "Heal", Role::Healer, 1},
            {first + 2, "Dps", Role::Dps, 1},
        };
        return request;
    }
}

TEST(DcRaidRosterReservationTest, ReservesWholeRosterAndReleasesIt)
{
    Store store;
    Request request = Roster();

    auto const result = store.TryReserve(request);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(store.Size(), 3u);
    EXPECT_TRUE(store.Contains(1));
    EXPECT_TRUE(store.Contains(3));

    store.Release(request);
    EXPECT_EQ(store.Size(), 0u);
    EXPECT_FALSE(store.Contains(1));
}

TEST(DcRaidRosterReservationTest, ConflictDoesNotPartiallyReserveRoster)
{
    Store store;
    ASSERT_TRUE(store.TryReserve(Roster(10)).ok);

    Request conflicting = Roster(9);
    auto const result = store.TryReserve(conflicting);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, Error::AlreadyReserved);
    EXPECT_EQ(store.Size(), 3u);
    EXPECT_FALSE(store.Contains(9));
    EXPECT_TRUE(store.Contains(10));
    EXPECT_TRUE(store.Contains(11));
}

TEST(DcRaidRosterReservationTest, DuplicateRequestIsRejectedWithoutMutation)
{
    Store store;
    Request duplicate = Roster();
    duplicate.members[2].guid = duplicate.members[0].guid;

    auto const result = store.TryReserve(duplicate);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, Error::DuplicateMember);
    EXPECT_EQ(store.Size(), 0u);
}

TEST(DcRaidRosterReservationTest, ReleasingUnknownRosterIsSafe)
{
    Store store;
    store.Release(Roster(100));
    EXPECT_EQ(store.Size(), 0u);
}
