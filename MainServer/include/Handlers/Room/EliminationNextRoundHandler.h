#ifndef ELIMINATION_NEXT_ROUND_HANDLER_H
#define ELIMINATION_NEXT_ROUND_HANDLER_H

#include "HaveApi/PacketLog.h"
#include "HaveApi/PartyMatch.h"
#include "HaveApi/PartyRewards.h"
#include "HaveApi/Sender.h"
#include <algorithm>
#include <array>
#include <format>
#include "Network/Sessions/MainSession.h"
#include "Structures/AccountInfo/MainAccountInfo.h"
#include "Network/Packet.h"
#include "Managers/RoomsManager.h"
#include "Structures/Match/EndScoreboard.h"
#include "Handlers/Room/RoomStartHandler.h"
#include "Detail/IpcUtils.h"
#include "Managers/PartiesManager.h"
#include "Handlers/Room/MatchEndRecords.h"

namespace Main
{
	namespace Handlers
	{
		// 160 sets the receiver's own variant (0xB00AD0); the others get it as 314 for the sender's unit (0xAF4F50)
		inline void handleCharacterVariant(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
			Main::Classes::RoomsManager& roomsManager)
		{
			auto* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber);
			if (!room || request.getDataSize() != 0)
			{
				return;
			}
			const std::uint8_t variant = static_cast<std::uint8_t>(request.getOption() & 7);

			Common::Network::Packet own;
			own.setTcpHeader(session->getId(), Common::Enums::USER_LARGE_ENCRYPTION);
			own.setCommand(160, 0, 0, variant);
			session->asyncWrite(own);

			const Main::Structures::UniqueId unit = session->getAccountInfo().uniqueId;
			Common::Network::Packet others;
			others.setTcpHeader(session->getId(), Common::Enums::USER_LARGE_ENCRYPTION);
			others.setCommand(314, 0, 0, variant);
			others.setData(reinterpret_cast<const std::uint8_t*>(&unit), sizeof(unit));
			room->broadcastToRoomExceptSelf(others, unit);
		}

		inline void handleEliminationNextRound(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
			Main::Classes::RoomsManager& roomsManager)
		{
			if (auto* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber))
			{
				if (!room->isHost(session->getAccountInfo().uniqueId)) return;
				room->broadcastToRoomExceptSelf(const_cast<Common::Network::Packet&>(request), session->getAccountInfo().uniqueId);
			}
		}

		inline void handleEliminationNextRound2(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
			Main::Classes::RoomsManager& roomsManager)
		{
			if (auto* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber))
			{
				if (!room->isHost(session->getAccountInfo().uniqueId)) return;
				Common::Network::Packet response = request;
				response.setExtra(1);
				room->broadcastToRoomExceptSelf(response, session->getAccountInfo().uniqueId);
			}
		}

		inline std::uint32_t pveRewardBoxFor(std::uint16_t respawnsLeft)
		{
			enum Rewards : std::uint32_t { GoldPveBox = 4801012, SilverPveBox = 4801013, BronzePveBox = 4801014 };
			if (respawnsLeft == 3) return GoldPveBox;
			if (respawnsLeft == 2) return SilverPveBox;
			return BronzePveBox;
		}

		inline void handleBossBattleEnding(Main::Classes::Room* room, Common::Network::Packet& response, bool isFarm)
		{
			if (!isFarm)
			{
				auto data = response.getData();
				isFarm = (response.getDataSize() > 8) ? (data[8] == 5) : true;
			}

			struct PlayerInfo
			{
				Main::Structures::UniqueId uid;
				std::uint32_t wonBoxId;
			};

			std::vector<PlayerInfo> playerInfos;

			const auto totalPlayers = static_cast<std::size_t>(room->getPlayersCount());
			if (totalPlayers <= playerInfos.max_size())
				playerInfos.reserve(totalPlayers);

			for (auto& [roomInfo, session] : room->getAllPlayersWithSessions())
			{
				if (roomInfo.team == Common::Enums::TEAM_OBSERVER ||
					!session->getPlayer().isInMatch())
				{
					continue;
				}

				playerInfos.push_back({
					session->getAccountInfo().uniqueId,
					pveRewardBoxFor(session->getPlayer().matchContext.totalBossBattleRespawnsLeft)
					});
			}

			struct PveResponseSelf
			{
				std::uint32_t totMP;
				std::uint32_t totEXP;
				std::uint32_t rewardId;
			} pveRespSelf;

			response.setExtra(isFarm ? 6 : 1);

			for (auto& [roomInfo, session] : room->getAllPlayersWithSessions())
			{
				if (roomInfo.team == Common::Enums::TEAM_OBSERVER ||
					!session->getPlayer().isInMatch())
				{
					continue;
				}

				pveRespSelf.totMP = session->getAccountInfo().microPoints;
				pveRespSelf.totEXP = session->getAccountInfo().experience;

				auto it = std::find_if(
					playerInfos.begin(),
					playerInfos.end(),
					[&](const PlayerInfo& info)
					{
						return info.uid == session->getAccountInfo().uniqueId;
					});

				pveRespSelf.rewardId = (it != playerInfos.end()) ? it->wonBoxId : 0;

				response.setData(
					reinterpret_cast<const std::uint8_t*>(&pveRespSelf),
					sizeof(pveRespSelf));

				session->asyncWrite(response);

				if (pveRespSelf.rewardId && !isFarm)
				{
					session->spawnItemCommand(
						pveRespSelf.rewardId,
						"Boss Battle reward spawned automatically after completing the boss battle mode");

					session->sendRt(2000);
					session->sendMessage("You obtained 2'000 RT and a Boss Battle reward!");
				}
			}

			if (isFarm)
				return;

			response.setData(nullptr, 0);
			response.setExtra(41);

			for (auto& [roomInfo, session] : room->getAllPlayersWithSessions())
			{
				if (roomInfo.team == Common::Enums::TEAM_OBSERVER)
					continue;

				const auto selfId = session->getAccountInfo().uniqueId;
				std::vector<PlayerInfo> filtered;
				filtered.reserve(playerInfos.size());

				for (const auto& pi : playerInfos)
				{
					if (pi.uid != selfId)
						filtered.push_back(pi);
				}

				const std::uint32_t count =static_cast<std::uint32_t>(filtered.size());

				std::vector<std::uint8_t> buffer;
				const std::size_t bufferSize =sizeof(count) + filtered.size() * sizeof(PlayerInfo);

				if (bufferSize <= buffer.max_size())
					buffer.reserve(bufferSize);

				buffer.insert(
					buffer.end(),
					reinterpret_cast<const std::uint8_t*>(&count),
					reinterpret_cast<const std::uint8_t*>(&count) + sizeof(count));

				if (!filtered.empty())
				{
					buffer.insert(
						buffer.end(),
						reinterpret_cast<const std::uint8_t*>(filtered.data()),
						reinterpret_cast<const std::uint8_t*>(filtered.data()) +
						filtered.size() * sizeof(PlayerInfo));
				}

				response.setData(buffer.data(), buffer.size());
				session->asyncWrite(response);
			}

			response.setData(nullptr, 0);
		}

		inline void handleSinglewaveEnding(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session)
		{
			const auto now = Main::Details::getUtcTimeMs();
			const Main::ClientData::SinglewaveEndRequest req = Main::Details::parseData<Main::ClientData::SinglewaveEndRequest>(request);

			if (req.type != 1 && req.type != 2)
			{
				session->completeTutorial();
				return;
			}

			Common::Network::Packet response = request;
			response.setCommand(66, 0, 17, 2); // single wave ack

			if (session->getPlayer().inventory.hasEnoughInventorySpace(1))
			{
				constexpr std::uint64_t threeMinsMs = 3 * 60 * 1000;
				constexpr std::uint64_t sevenMinsMs = 7 * 60 * 1000;

				if (req.type == 1 && req.stage == 10 && (now - session->getPlayer().matchContext.matchStartTime >= threeMinsMs))
				{
					Main::Structures::BoughtItem reward{ Common::Constants::singlewaveEasyBox };
					reward.serialInfo.itemNumber = session->getPlayer().inventory.getLatestItemNumber() + 1;
					session->getPlayer().inventory.setLatestItemNumber(reward.serialInfo.itemNumber);
					response.setData(reinterpret_cast<std::uint8_t*>(&reward), sizeof(reward));
					session->addItem(Main::Structures::Item{ reward });
					session->getPlayer().matchContext.matchStartTime = now;
				}
				else if (req.type == 2)
				{
					if (req.stage == 20 && (now - session->getPlayer().matchContext.matchStartTime >= sevenMinsMs))
					{
						Main::Structures::BoughtItem reward{ Common::Constants::singlewaveHardBox };
						reward.serialInfo.itemNumber = session->getPlayer().inventory.getLatestItemNumber() + 1;
						session->getPlayer().inventory.setLatestItemNumber(reward.serialInfo.itemNumber);
						response.setData(reinterpret_cast<std::uint8_t*>(&reward), sizeof(reward));
						session->addItem(Main::Structures::Item{ reward });
						session->getPlayer().matchContext.matchStartTime = now;
					}
					session->updateSingleWaveScore(req.score, req.stage);
				}
			}
			session->asyncWrite(response);
		}

		inline void storeClanMatchStats(Main::Classes::Room* room, std::shared_ptr<Main::Network::Session> session,
			Main::Classes::PartiesManager& partiesManager, Main::Persistence::MainScheduler& scheduler,
			const Main::ClientData::ClientEndingMatchHeader& endMatchHeader)
		{
			auto matchResult = partiesManager.getClanMatch(room->getRoomNumber());
			if (!matchResult)
			{
				session->sendMessage("[Main::Handlers::handleMatchEnding] error while retrieving clan match!");
				return;
			}

			auto& [partyRoomA, partyRoomB] = *matchResult;
			if (partyRoomA)
			{
				partyRoomA->updatePartyStatus(false);
				partyRoomA->storeStats(scheduler, endMatchHeader);
			}
			if (partyRoomB)
			{
				partyRoomB->updatePartyStatus(false);
				partyRoomB->storeStats(scheduler, endMatchHeader);
			}
		}

		inline bool isFarmingMatch(Main::Classes::Room* room, std::uint64_t timeNow)
		{
			const std::uint64_t roomStartTime = room->getMatchStartTime();
			const bool shortOrUnranked =
				(timeNow > roomStartTime && (timeNow - roomStartTime) < 80 * 1000)
				|| (room->getRoomSettings().mode == Common::Enums::SquareMode && !room->haveApi().hasPartyGame()) // party games: normal rewards
				|| room->getRoomSettings().mode == Common::Enums::AiBattle;

			if (!shortOrUnranked) return false;
			return room->getRoomNumber() < Common::Constants::clanRoomNumberStart; // don't count farming in cw
		}

		inline void processEndMatchPlayer(Main::Structures::ClientEndingMatch clientScore,
			const Main::ClientData::ClientEndingMatchHeader& endMatchHeader, Main::Classes::Room* room,
			Common::Network::Packet& response, std::uint16_t requestOrder, std::uint64_t timeNow, bool isFarm,
			const Main::Structures::ExpMpBonusInfo& expMpBonusInfo, const Main::Structures::EventMissionInfo& eventMissionInfo)
		{
			Main::Structures::ScoreboardResponse scoreboardResponse(clientScore);

			auto targetSession = room->getPlayer(clientScore.uniqueId);
			if (!targetSession) return;

			const Main::Structures::AccountInfo ainfo = targetSession->getAccountInfo();
			if (room->getRoomNumber() >= Common::Constants::clanRoomNumberStart)
			{
				const auto totalNewContribution = Common::Constants::clanBaseContribution + scoreboardResponse.totalKills * 6;
				scoreboardResponse.newTotalClanContribution = ainfo.clanContribution +
					(totalNewContribution <= Common::Constants::maxExpAndMpPerMatch ? totalNewContribution : Common::Constants::maxExpAndMpPerMatch);
			}

			std::uint32_t totalExpBonus = 0;
			std::uint32_t totalMpBonus = 0;
			const auto& equippedItems = targetSession->getPlayer().inventory.getEquippedItemsFor(targetSession->getAccountInfo().latestSelectedCharacter);
			for (const auto& currentItem : equippedItems)
			{
				if (currentItem.serialInfo.itemNumber == 0) continue;
				const auto [expBonus, mpBonus] = Main::CdbUtils::getExpAndMpEnhancementFor(currentItem.id);
				totalExpBonus += expBonus;
				totalMpBonus += mpBonus;
			}

			auto baseMp = (scoreboardResponse.totalKills * 15 + scoreboardResponse.deaths * 5 + Common::Constants::matchBaseMp) * 2;
			auto baseExp = (scoreboardResponse.totalKills * 15 + scoreboardResponse.deaths * 5 + Common::Constants::matchBaseExp) * 2;

			const auto* partyResult = room->haveApi().partyResultOf(static_cast<std::uint32_t>(targetSession->getId()));
			const std::uint64_t elapsedMinutes = Main::HaveApi::playedMinutes(targetSession->getPlayer().matchContext.matchStartTime, timeNow,
				partyResult != nullptr ? partyResult->playedUntil : timeNow, 15);
			constexpr std::uint64_t mpPerMinute = 80;
			constexpr std::uint64_t expPerMinute = 80;
			const std::uint64_t bonusMinutes = partyResult != nullptr ? Main::HaveApi::placementBonusMinutesFor(partyResult->placement, partyResult->entrants) : 0;
			auto gainedMp = isFarm ? 0 : baseMp + (elapsedMinutes + bonusMinutes) * mpPerMinute;
			auto gainedExp = isFarm ? 0 : baseExp + (elapsedMinutes + bonusMinutes) * expPerMinute;
			auto finalGainedExp = gainedExp + (gainedExp * totalExpBonus / 100);
			auto finalGainedMp = gainedMp + (gainedMp * totalMpBonus / 100);

			const std::uint64_t now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			auto mode = room->getRoomSettings().mode;
			std::uint32_t finalGainedExpWithEvent = mode == Common::Enums::FreeForAll ? (finalGainedExp / 1.3) : finalGainedExp;
			std::uint32_t finalGainedMpWithEvent = mode == Common::Enums::FreeForAll ? (finalGainedMp / 1.3) : finalGainedMp;

			if (now <= expMpBonusInfo.endDate)
			{
				finalGainedExpWithEvent += (finalGainedExp * expMpBonusInfo.expBonusPercent / 100);
				finalGainedMpWithEvent += (finalGainedMp * expMpBonusInfo.mpBonusPercent / 100);
			}

			const auto clampedExp = (finalGainedExpWithEvent <= Common::Constants::maxExpAndMpPerMatch) ? finalGainedExpWithEvent
				: Common::Constants::maxExpAndMpPerMatch;
			const auto clampedMp = (finalGainedMpWithEvent <= Common::Constants::maxExpAndMpPerMatch) ? finalGainedMpWithEvent
				: Common::Constants::maxExpAndMpPerMatch;
			if (!isFarm && (clampedExp + ainfo.experience) < ainfo.experience)
			{
				targetSession->sendMessage("[Handlers::handleEliminationNextRound] error: negative experience detected");
				scoreboardResponse.newTotalEXP = ainfo.experience + 200;
			}
			else
			{
				scoreboardResponse.newTotalEXP = ainfo.experience + clampedExp;
			}
			if (!isFarm && (clampedMp + ainfo.microPoints) < ainfo.microPoints)
			{
				targetSession->sendMessage("[Handlers::handleEliminationNextRound] error: negative MP detected");
				scoreboardResponse.newTotalMP = ainfo.microPoints + 200;
			}
			else
			{
				scoreboardResponse.newTotalMP = ainfo.microPoints + clampedMp;
			}

			if (auto* gradeInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbGradeInfo>::getInstance().getEntry(ainfo.playerLevel + 1);
				!isFarm && gradeInfo && scoreboardResponse.newTotalEXP >= gradeInfo->gi_exp)
			{ // the player leveled up
				targetSession->sendMessage("You obtained a reward box!", Main::Enums::TIP);

				const std::uint32_t actualPlayerLevel = ainfo.playerLevel;
				const std::uint64_t newPlayerLevel = actualPlayerLevel + 1;

				response.setCommand(311, 0, 1, newPlayerLevel);
				response.setData(reinterpret_cast<std::uint8_t*>(&clientScore.uniqueId), sizeof(clientScore.uniqueId));
				room->broadcastToRoomExceptSelf(response, clientScore.uniqueId);

				targetSession->spawnItemCommand((actualPlayerLevel % 10 == 0) ? Common::Constants::goldLevelBox
					: (actualPlayerLevel % 5 == 0) ? Common::Constants::silverLevelBox : Common::Constants::bronzeLevelBox,
					"Item spawned automatically (Level-up reward item)");

				scoreboardResponse.newTotalMP += gradeInfo->gi_reward_point;
				room->storeEndMatchStatsFor(clientScore.uniqueId, scoreboardResponse, endMatchHeader.blueScore, endMatchHeader.redScore, true,
					eventMissionInfo);

				if (actualPlayerLevel >= 5 && actualPlayerLevel % 5 == 0)
				{ // RT & coupon reward
					const std::uint32_t rtToAdd = 2000 * (actualPlayerLevel / 5);
					targetSession->sendRt(rtToAdd);
					targetSession->spawnCouponImmediate(5);
					targetSession->sendMessage("You obtained " + std::to_string(rtToAdd) + " RockTokens and 5 coupons!");
				}
			}
			else
			{
				if (!isFarm)
				{
					room->storeEndMatchStatsFor(clientScore.uniqueId, scoreboardResponse, endMatchHeader.blueScore, endMatchHeader.redScore, false,
						eventMissionInfo);
				}
			}
			response.setCommand(requestOrder, 3, isFarm ? 6 : 1, 0);
			response.setData(reinterpret_cast<std::uint8_t*>(&scoreboardResponse), sizeof(scoreboardResponse));
			targetSession->asyncWrite(response);
			if (!isFarm)
			{
				targetSession->sendRt(static_cast<std::uint32_t>(static_cast<double>(clampedMp)/3));
				if (room->getRoomSettings().mode != Common::Enums::SquareMode && room->getRoomSettings().mode != Common::Enums::AiBattle)
				{
					targetSession->reduceEquippedItemsDurability(room->getRoomSettings().weaponRestriction);
				}
			}
		}

		template<typename Record>
		std::vector<Record> readRoomMatchEndRecords(const Common::Network::Packet& request, Main::Classes::Room* room, std::size_t headerSize,
			std::size_t declared)
		{
			auto records = readMatchEndRecords<Record>({ request.getData(), request.getDataSize() }, headerSize, declared,
				[room](const Main::Structures::UniqueId& uniqueId) { return room->getPlayer(uniqueId) != nullptr; });
			if (!records.clean())
			{
				Utils::Logger::log("[Handlers::handleMatchEnding] room " + std::to_string(room->getRoomNumber()) + ": match end records skipped ("
					+ records.anomalies() + ")", Utils::LogType::Warning);
			}
			return std::move(records.players);
		}

		// Scrimmage (MOC): the host's match end has the mode's own format (ScrimmageEndingHeader + one ScrimmageEndingPlayer
		// per player), not the header + ClientEndingMatch records of the other modes: read that way it named no player,
		// so nobody got the personal scoreboard (EXP, MP, kills) and the result screen kept the client's defaults.
		// Each player's scoreboard: the kills / deaths the server counted (Cast death reports), the win / loss from the
		// header's winner (what every client's result screen shows).
		inline void handleScrimmageEnding(const Common::Network::Packet& request, Main::Classes::Room* room,
			Common::Network::Packet& response, std::uint64_t timeNow, bool isFarm,
			const Main::Structures::ExpMpBonusInfo& expMpBonusInfo, const Main::Structures::EventMissionInfo& eventMissionInfo)
		{
			using Header = Main::ClientData::ScrimmageEndingHeader;
			using Record = Main::ClientData::ScrimmageEndingPlayer;
			const auto header = Main::Details::parseData<Header>(request);
			const auto records = readRoomMatchEndRecords<Record>(request, room, sizeof(Header), header.players);
			Main::ClientData::ClientEndingMatchHeader result{};
			result.redScore = header.winner == Common::Enums::TEAM_RED ? 1 : 0;
			result.blueScore = header.winner == Common::Enums::TEAM_BLUE ? 1 : 0;
			Utils::Logger::log("[Handlers::handleScrimmageEnding] room " + std::to_string(room->getRoomNumber()) + ": winner "
				+ std::to_string(header.winner) + ", pools red " + std::to_string(header.redPool) + " blue "
				+ std::to_string(header.bluePool) + ", " + std::to_string(records.size()) + " players");
			for (const auto& record : records)
			{
				const auto player = room->getPlayer(record.uniqueId);
				if (!player) continue;
				const auto counted = room->haveApi().killCountOf(player->getAccountInfo().accountID);
				Main::Structures::ClientEndingMatch score{};
				score.uniqueId = record.uniqueId;
				score.totalKills = std::min<std::uint32_t>(counted.kills, 255);
				score.totalDeaths = std::min<std::uint32_t>(counted.deaths, 255);
				processEndMatchPlayer(score, result, room, response, request.getOrder(), timeNow, isFarm, expMpBonusInfo, eventMissionInfo);
			}
		}

		inline void handleMatchEnding(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
			Main::Classes::RoomsManager& roomsManager,
			Main::Classes::PartiesManager& partiesManager, Main::Persistence::MainScheduler& scheduler, const Main::Structures::ExpMpBonusInfo& expMpBonusInfo,
			const Main::Structures::EventMissionInfo& eventMissionInfo)
		{
			if (request.getExtra() == 6)
			{
				handleSinglewaveEnding(request, session);
				return;
			}

			Main::HaveApi::PacketLog::writeGamePacket("match-end C->S", session->getId(), Main::HaveApi::playerName(*session), request.getOrder(),
				request.getMission(), request.getExtra(), request.getOption(), { request.getData(), request.getDataSize() });
			Main::Classes::Room* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber);
			if (!room) return;
			Main::HaveApi::logClientMatchEnd(*room, *session, std::format("{} records, room match {}", request.getOption(),
				room->hasMatchStarted() ? "started" : "not started or already ended (ignored)"));
			if (!room->isHost(session->getAccountInfo().uniqueId)) return; // only the host should send this packet, prevent lvl up exploits
			if (!room->hasMatchStarted())
			{
				Utils::Logger::log("[Handlers::handleMatchEnding] room " + std::to_string(room->getRoomNumber()) + ": match end from host "
					+ std::string{ Main::HaveApi::playerName(*session) } + " with no match running (not started or already ended), ignored",
					Utils::LogType::Warning);
				return;
			}

			Common::Network::Packet response = request;
			const Main::ClientData::ClientEndingMatchHeader endMatchHeader = Main::Details::parseData<Main::ClientData::ClientEndingMatchHeader>(request);

			if (room->getRoomNumber() >= Common::Constants::clanRoomNumberStart)
			{
				storeClanMatchStats(room, session, partiesManager, scheduler, endMatchHeader);
			}

			const std::uint64_t timeNow = Main::Details::getUtcTimeMs();
			const bool isFarm = isFarmingMatch(room, timeNow);

			// client sends all info of all players, we resend it back to everyone (else the other clients outside the match will see the target still inside the match)
			room->broadcastToRoomExceptSelf(response, session->getAccountInfo().uniqueId);

			if (room->getRoomSettings().mode == Common::Enums::BossBattle)
			{
				handleBossBattleEnding(room, response, isFarm);
				room->endMatch();
				return;
			}

			room->endMatch();

			if (room->getRoomSettings().mode == Common::Enums::Scrimmage)
			{
				handleScrimmageEnding(request, room, response, timeNow, isFarm, expMpBonusInfo, eventMissionInfo);
				return;
			}

			for (const auto& clientScore : readRoomMatchEndRecords<Main::Structures::ClientEndingMatch>(request, room, sizeof(endMatchHeader),
				request.getOption()))
			{
				processEndMatchPlayer(clientScore, endMatchHeader, room, response, request.getOrder(), timeNow, isFarm, expMpBonusInfo, eventMissionInfo);
			}
		}
	}
}

#endif
