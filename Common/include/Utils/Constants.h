#ifndef GAME_CONSTANTS_HEADER
#define GAME_CONSTANTS_HEADER

#include <cstdint>
#include <string>

namespace Common
{
	namespace Constants
	{
		// IPC handler ids
		constexpr inline std::uint32_t A2M_getPlayersPerServer = 1;
		constexpr inline std::uint32_t M2C_mapId = 2;
		constexpr inline std::uint32_t M2C_roomNumber = 3;
		constexpr inline std::uint32_t C2M_castConnect = 4;
		constexpr inline std::uint32_t C2M_updatePlayerState = 5;
		constexpr inline std::uint32_t A2M_passIp = 6;
		constexpr inline std::uint32_t M2C_playerTeamInfoBatch = 8;
		constexpr inline std::uint32_t M2C_Invisibility = 9;
		constexpr inline std::uint32_t C2M_CloseSocketReq = 10;
		constexpr inline std::uint32_t A2M_disconnectOnlinePlayer = 11;
		constexpr inline std::uint32_t C2M_partyCombatEvent = 12; // have_api party matches: deaths and respawns
		constexpr inline std::uint32_t M2C_partyKillPlayer = 13;  // have_api party matches: death packet to one player only
		constexpr inline std::uint32_t M2C_partyLava = 14;        // have_api Lava Escape: Common::HaveApi::LavaCommand -> LavaState
		constexpr inline std::uint32_t M2C_partyZone = 15;        // have_api Hardpoint: Common::HaveApi::ZoneCommand -> ZoneAnswer
		constexpr inline std::uint32_t C2M_partyZoneOccupants = 16; // have_api Hardpoint: ZoneAnswer, sent when the occupants change
		constexpr inline std::uint32_t M2C_tickRate = 17;
		// the CastServer's persistent link to the MainServer announces itself: the MainServer sends its commands on it
		// (Main::Ipc::CastLink)
		constexpr inline std::uint32_t C2M_linkHello = 19;
		constexpr inline std::uint32_t M2C_roomSync = 20;
		constexpr inline std::uint32_t C2M_roomSyncRequest = 21;
		constexpr inline std::uint32_t M2C_partyStorm = 22;        // have_api Battle Royale: Common::HaveApi::StormCommand -> StormAnswer
		constexpr inline std::uint32_t C2M_partyStormOutside = 23; // have_api Battle Royale: StormAnswer, sent when the players outside change
		constexpr inline std::uint32_t M2C_partyMatchInfo = 24;    // have_api party matches: Common::HaveApi::PartyMatchInfo (game and round, for the mode logs)

		// Rest
		constexpr inline std::uint16_t maxSessionsPerServer = 500;
		constexpr inline std::uint16_t maxServerCapacity = 300;
		constexpr inline std::uint16_t maxNicknameSize = 16;
		constexpr inline std::uint16_t maxEquippedItems = 17;
		constexpr inline std::uint16_t maxMailbox = 100;
		constexpr inline std::uint16_t maxPacketBytes = 1440;
		constexpr inline std::uint16_t headerSize = 8;
		constexpr inline std::uint16_t maxRoomPlayers = 26; // 16 + 10 observers
		constexpr inline std::uint16_t maxObserverPlayers = 10;
		constexpr inline std::uint16_t maxRoomPlayersNoObs = 16;
		constexpr inline std::uint16_t maxFriends = 59;
		constexpr inline std::uint16_t maxMailboxMessage = 256;
		constexpr inline std::uint16_t upgradeFailRate = 15;
		constexpr inline std::uint32_t maxItemProbability = 1'000'000;
		constexpr inline double capsulePriceFactor = 0.2;
		constexpr inline std::uint16_t maxLuckySpin = 1000;
		constexpr inline std::uint16_t maxItemType = 30;
		constexpr inline std::uint16_t maxRooms = 30;
		constexpr inline std::uint16_t maxPassword = 9;
		constexpr inline std::uint16_t maxClanRooms = 30;
		constexpr inline std::uint16_t maxPartiesPerClan = 4;
		constexpr inline std::uint32_t clanRoomNumberStart = 151;
		constexpr inline std::uint16_t matchBaseExp = 80;
		constexpr inline std::uint16_t matchBaseMp = 150;
		constexpr inline std::uint16_t maxExpAndMpPerMatch = 4000;
		constexpr inline std::uint16_t clanBaseContribution = 100;
		constexpr inline std::uint32_t goldLevelBox = 5336571;
		constexpr inline std::uint32_t silverLevelBox = 5336570;
		constexpr inline std::uint32_t bronzeLevelBox = 5336569;
		constexpr inline std::uint32_t tutorialBox = 4500000;
		constexpr inline std::uint32_t singlewaveEasyBox = 5337005;
		constexpr inline std::uint32_t singlewaveHardBox = 5337006;
		constexpr inline std::uint16_t eventMissionTotal = 36;
		constexpr inline std::uint16_t totalEventMissions = 5;
		inline std::string teamString = "Team";
	}
}
#endif
