/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */

#ifndef MAIN_HAVE_API_PROFILE_H
#define MAIN_HAVE_API_PROFILE_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include "ConstantDatabase/CdbSingleton.h"
#include "ConstantDatabase/Structures/CdbGradeInfo.h"
#include "Enums/PlayerEnums.h"
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Messages.h"
#include "HaveApi/Sender.h"
#include "Network/Sessions/MainSession.h"

// have_api profile (message 0x05): the info dialog's "Title" line is the title the player chose (/title,
// Users.Title), the "Grade" line (the client's label, make_party_titles.py UI_TEXTS) the player's grade by account
// grade; level and experience from the account and gradeinfo.cdb.
//  - the player's own profile: at login and after /title, forDetailDialog 1 (the game's own-player fill, 0x63E846);
//  - another player's profile: after the server's answer with that player's lobby info (the game fills E_DLG_INFO
//    from it, 0x6408E0, then the DLL writes title / grade / level / experience into the open dialog), forDetailDialog 0.
namespace Main::HaveApi
{
	inline constexpr std::size_t maxTitleLength = 20;

	// The grade shown under the title: 1 Player, 2 EventSupporter, 3 Moderator, 4 GameMaster, 7 Developer; the other
	// grades show as Player (see doc/have_api_server_open_questions.md D70).
	[[nodiscard]] inline std::string_view gradeName(std::uint32_t grade)
	{
		switch (grade)
		{
		case 2: return "EventSupporter";
		case 3: return "Moderator";
		case 4: return "GameMaster";
		case 7:
		case 8: return "Developer";
		case 9: return "System";
		default: return "Player";
		}
	}

	// Letters and single spaces between words, 1..maxTitleLength characters; empty: no title.
	[[nodiscard]] inline bool isValidTitle(std::string_view title)
	{
		if (title.empty())
		{
			return true;
		}
		if (title.size() > maxTitleLength || title.front() == ' ' || title.back() == ' ' || title.find("  ") != std::string_view::npos)
		{
			return false;
		}
		return std::ranges::all_of(title, [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == ' '; });
	}

	[[nodiscard]] inline Common::HaveApi::ProfilePayload profileOf(Main::Network::Session& player, bool forDetailDialog)
	{
		namespace CH = Common::HaveApi;
		using Levels = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbGradeInfo>;
		const auto& account = player.getAccountInfo();
		CH::ProfilePayload profile{};
		profile.forDetailDialog = forDetailDialog ? 1 : 0;
		const std::string& title = player.getPlayer().title;
		const std::string_view shownTitle = title.empty() ? std::string_view{ "-" } : std::string_view{ title };
		std::copy_n(shownTitle.begin(), std::min(shownTitle.size(), profile.title.size() - 1), profile.title.begin());
		const std::string_view grade = gradeName(static_cast<std::uint32_t>(account.playerGrade));
		std::copy_n(grade.begin(), std::min(grade.size(), profile.subtitle.size() - 1), profile.subtitle.begin());
		const auto key = static_cast<std::size_t>(account.playerLevel); // gi_grade of the player's level
		const auto* current = Levels::getInstance().getEntry(key);
		const auto* next = Levels::getInstance().getEntry(key + 1);
		const auto total = static_cast<std::int32_t>(account.experience);
		const std::int32_t start = current != nullptr ? static_cast<std::int32_t>(current->gi_exp) : 0;
		const std::int32_t end = next != nullptr ? static_cast<std::int32_t>(next->gi_exp) : start; // the last level
		// -1: the DLL leaves line 110186 alone (it is E_DLG_INFO's "single wave record", not a level; the game shows the
		// level by the name). Experience as the game's own dialog (0x63DF30): bar = exp - level start of next - level start,
		// text = total and next level start - total.
		profile.level = -1;
		profile.experience = (std::max)(0, total - start);
		profile.nextLevelExperience = (std::max)(profile.experience, end - start);
		profile.totalExperience = total;
		return profile;
	}

	inline void sendProfile(Main::Network::Session& receiver, Main::Network::Session& player, bool forDetailDialog)
	{
		namespace CH = Common::HaveApi;
		sendToPlayer(receiver, CH::encode(CH::MessageId::Profile, profileOf(player, forDetailDialog)));
	}
}

#endif
