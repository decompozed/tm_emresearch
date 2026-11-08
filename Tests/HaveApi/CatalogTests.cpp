/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#include <gtest/gtest.h>
#include "HaveApi/Catalog.h"

namespace CH = Common::HaveApi;

TEST(HaveApiCatalog, DefaultsOfEveryModeAreValid)
{
	for (const auto mode : { CH::PartyModeId::ChickenRun, CH::PartyModeId::Chaser, CH::PartyModeId::Arena, CH::PartyModeId::TeamClash,
			 CH::PartyModeId::LavaEscape })
	{
		const CH::PartySettingsPayload settings{ static_cast<std::int32_t>(mode), CH::defaultSettings(mode) };
		EXPECT_TRUE(CH::isValidPartySettings(settings)) << static_cast<int>(mode);
	}
}

TEST(HaveApiCatalog, DefaultsMatchTheDll)
{
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::ChickenRun), (CH::SettingValues{ 20, 2, 1, 0, 0, 0 }));
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::Chaser), (CH::SettingValues{ 20, 0, 0, 0, 0, 0 }));
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::Arena), (CH::SettingValues{ 20, 2, 1, 0, 0, 0 }));
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::TeamClash), (CH::SettingValues{ 30, 2, 1, 3, 0, 0 }));
	EXPECT_EQ(CH::defaultSettings(CH::PartyModeId::LavaEscape), (CH::SettingValues{ 8, 2, 1, 1, 1, 0 }));
}

TEST(HaveApiCatalog, SettingsSentByTheDllAtLogin)
{
	// captured from the DLL: mode 1, values 20, 2, 1
	EXPECT_TRUE(CH::isValidPartySettings({ 1, { 20, 2, 1, 0, 0, 0 } }));
}

TEST(HaveApiCatalog, RejectsInvalidPartySettings)
{
	EXPECT_FALSE(CH::isValidPartySettings({ 6, {} }));                      // unknown mode
	EXPECT_FALSE(CH::isValidPartySettings({ -1, {} }));
	EXPECT_FALSE(CH::isValidPartySettings({ 2, { 10, 0, 0, 0, 0, 0 } }));   // 10 is a Chicken Run round time only
	EXPECT_FALSE(CH::isValidPartySettings({ 4, { 30, 2, 1, 6, 0, 0 } }));   // 6 teams
	EXPECT_FALSE(CH::isValidPartySettings({ 3, { 20, 2, 1, 7, 0, 0 } }));   // unused slot not 0
	EXPECT_FALSE(CH::isValidPartySettings({ 0, { 20, 0, 0, 0, 0, 0 } }));   // "no party game" carries no values
	EXPECT_TRUE(CH::isValidPartySettings({ 0, {} }));
}

TEST(HaveApiCatalog, RoomOptions)
{
	EXPECT_TRUE(CH::isValidRoomOption(CH::RoomOptionKind::MaxHealth, 1500));
	EXPECT_FALSE(CH::isValidRoomOption(CH::RoomOptionKind::MaxHealth, 1200));
	EXPECT_TRUE(CH::isValidRoomOption(CH::RoomOptionKind::WeaponBalance, 2));
	EXPECT_FALSE(CH::isValidRoomOption(CH::RoomOptionKind::WeaponBalance, 0));
	EXPECT_TRUE(CH::isValidRoomOption(CH::RoomOptionKind::OptionCheckBox, 0));
	EXPECT_FALSE(CH::isValidRoomOption(CH::RoomOptionKind::OptionCheckBox, 2));
	EXPECT_FALSE(CH::isValidRoomOption(static_cast<CH::RoomOptionKind>(9), 1));
	EXPECT_TRUE(CH::roomOptionsApplyIn(12));
	EXPECT_FALSE(CH::roomOptionsApplyIn(13));
}
