#ifndef ROOM_START_HANDLER_HEADER
#define ROOM_START_HANDLER_HEADER

#include "HaveApi/TickRate.h"
#include "Network/Sessions/MainSession.h"
#include "Structures/AccountInfo/MainAccountInfo.h"
#include "Network/Packet.h"
#include "Managers/RoomsManager.h"
#include "Rooms/PartyRoom.h"
#include "Managers/PartiesManager.h"
#include <algorithm>
#include <cstring>
#include "Detail/IpcUtils.h"
#include "HaveApi/PartyAccess.h"
#include "HaveApi/PartyMatch.h"
#include "HaveApi/RoomState.h"

namespace Main
{
	namespace Handlers
	{
		inline void handleRoomStart(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session, Main::Classes::RoomsManager& roomsManager,
			Main::Classes::PartiesManager& partiesManager, std::uint64_t timeSinceLastServerRestart, Main::MainServer& mainServer)
		{
			if (request.getExtra() == 6)
			{ // Single wave
				session->getPlayer().matchContext.matchStartTime = Main::Details::getUtcTimeMs();
				return;
			}
			else if (Main::Classes::Room* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber))
			{
				Common::Network::Packet response;
				response.setTcpHeader(request.getSession(), Common::Enums::NO_ENCRYPTION);
				const Main::Structures::UniqueId selfUniqueId = session->getAccountInfo().uniqueId;

				if (request.getExtra() == 38) 
				{ // host or non-host clicks on "start" button 
					if (room->haveApi().hasPartyGame() && room->getRoomSettings().mode == Common::HaveApi::partyRoomMode &&
						room->getTeamForSession(static_cast<std::uint32_t>(session->getId())).value_or(0) == Common::Enums::TEAM_OBSERVER)
					{ // have_api party games have no observer view
						session->sendMessage("Observers cannot join party games.");
						return;
					}
					if (room->isCsdMode() && !session->hasCsdItems())
					{
						session->sendMessage("Error: This room requires CSD equipment.");
						return;
					}
					if (room->isHost(selfUniqueId))
					{
						if (room->getRoomSettings().mode == Common::Enums::BossBattle && room->getAllPlayers().size() > 4)
						{
							session->sendMessage("Error: Boss battle can only be started when there are 4 players (or less)");
							return;
						}
						if (room->isCsdMode() && !room->isEveryoneCsd()) return;
						if (room->haveApi().hasPartyGame() && room->getRoomSettings().mode == Common::HaveApi::partyRoomMode)
						{ // have_api party games need at least 2 players: the host and a ready player
							const auto players = room->getAllPlayers();
							const auto starting = std::ranges::count_if(players, [&](const Main::Structures::RoomPlayerInfo& info) {
								return info.team != Common::Enums::TEAM_OBSERVER &&
									(info.state == Common::Enums::STATE_READY || info.uniqueId.session == selfUniqueId.session);
							});
							if (starting < 2)
							{
								session->sendMessage("Party games need at least 2 players (the host and a ready player).");
								return;
							}
							const std::int32_t partyMode = room->haveApi().partySettings().modeId;
							if (!Main::HaveApi::isPartyModeOffered(partyMode))
							{
								session->sendMessage("Battle Royale is not enabled on this server");
								return;
							}
							if (const auto member = Main::HaveApi::memberWithoutSupport(*room, partyMode))
							{
								session->sendMessage(std::string{ Main::HaveApi::playerName(*member) } + " needs the updated have_api.dll for this party game");
								return;
							}
						}
						room->generateMapIfRandom();

						if (!Main::Ipc::M2C_sendMapId(selfUniqueId.session, room->getActualMap(), room->getRoomSettings().mode))
						{
							session->sendMessage("[Main::HandleRoomStart] internal server error (IPC)!");
							return;
						}
						Main::Ipc::M2C_sendServerCommand(Common::Constants::M2C_tickRate, selfUniqueId.session,
							static_cast<std::uint8_t>(Common::HaveApi::currentTickRate.load()));
						if (room->getRoomNumber() >= Common::Constants::clanRoomNumberStart)
						{
							auto matchResult = partiesManager.getClanMatch(room->getRoomNumber());
							if (!matchResult)
							{
								session->sendMessage("[Main::HandleRoomStart] error while retrieving clan match!");
								return;
							}

							auto& [partyRoomA, partyRoomB] = *matchResult;
							if (partyRoomA)
							{
								partyRoomA->updatePartyStatus(true);
							}
							if (partyRoomB)
							{
								partyRoomB->updatePartyStatus(true);
							}
						}
						if (room->getRoomSettings().mode == Common::Enums::BossBattle && !session->removeBossBattleTicket())
						{
							session->sendMessage("[Handlers::handleRoomStart] Could not find boss battle ticket - if this is an error, report it");
							return;
						}
						room->startMatch();
						Main::HaveApi::startPartyMatch(mainServer, roomsManager, *room);
					}
					else if (room->hasMatchStarted())
					{
						if (const Main::HaveApi::PartyMatch* partyMatch = room->haveApi().match(); partyMatch && !partyMatch->acceptsLateJoiners() && !partyMatch->isParticipant(*session))
						{
							session->sendMessage("The Battle Royale has started: wait for the next match.");
							return;
						}
						session->getPlayer().matchContext.totalBossBattleRespawnsLeft = 3;
						room->setStateFor(selfUniqueId, Common::Enums::PlayerState::STATE_NORMAL);

						Main::ClientData::PlayerTeamInfo info;
						info.uid = selfUniqueId;
						std::memcpy(info.nickname, session->getAccountInfo().nickname, 16);
						if (auto foundTeam = room->getTeamForSession(selfUniqueId.session))
						{
							info.team = *foundTeam;
						}
						else
						{
							Utils::Logger::log("[Handlers::StartMatch] Failed to retrieve player team for IPC", Utils::LogType::Error);
							return;
						}
						if (!Main::Ipc::M2C_sendPlayerTeamInfoBatch(session->getId(), { info }))
						{
							Utils::Logger::log("[Handlers::StartMatch] Failed to send single player team info to Cast Server", Utils::LogType::Error);
						}
						if (Main::HaveApi::PartyMatch* partyMatch = room->haveApi().match())
						{
							partyMatch->onPlayerJoined(*session);
						}
					}

					session->getPlayer().matchContext.matchStartTime = Main::Details::getUtcTimeMs();
					response.setCommand(request.getOrder(), 0, 38, room->getActualMap());
					response.setData(reinterpret_cast<const std::uint8_t*>(&selfUniqueId), sizeof(selfUniqueId));
					room->broadcastToRoom(response);
				}
				else if (request.getExtra() == 41)
				{
					if (Main::HaveApi::PartyMatch* partyMatch = room->haveApi().match())
					{
						partyMatch->onPlayerLoaded(*session);
					}
					if (room->isHost(selfUniqueId)) 
					{ // broadcast the tick to the room
						const std::uint64_t roomTick = Details::getUtcTimeMs() - timeSinceLastServerRestart;
						response.setCommand(258, 0, 1, 0);  // What's extra 5 here?
						response.setData(reinterpret_cast<const std::uint8_t*>(&roomTick), sizeof(roomTick));
						room->broadcastToRoom(response);
					}
					else 
					{ // Tell the other players in the match that we joined
						response.setCommand(415, 0, 1, 0);
						response.setData(reinterpret_cast<const std::uint8_t*>(&selfUniqueId), sizeof(selfUniqueId));
						if (!session->getPlayer().matchContext.isInvisible)
						{
							room->broadcastToRoom(response);
						}
						else
						{
							session->asyncWrite(response);
						}
					}
					
				}
			}
		}

		inline bool handleRoomStartInvisible(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session,
			Main::Classes::RoomsManager& roomsManager)
		{
			if (Main::Classes::Room* room = roomsManager.getRoomByNumber(session->getPlayer().matchContext.roomNumber))
			{
				Common::Network::Packet response;
				response.setTcpHeader(request.getSession(), Common::Enums::NO_ENCRYPTION);
				const Main::Structures::UniqueId selfUniqueId = session->getAccountInfo().uniqueId;

				if (request.getExtra() == 38)
				{ // host or non-host clicks on "start" button (n.b: SingleWave's extra is 6)
					if (room->isHost(selfUniqueId))
					{
						session->sendMessage("Error: use this command when you are not the host");
						return false;
					}
					if (session->getPlayer().matchContext.roomNumber >= Common::Constants::clanRoomNumberStart && !room->hasMatchStarted())
					{
						session->sendMessage("Error: You can only enter clan matches if they have started!");
						return false;
					}
					else if (room->hasMatchStarted())
					{
						room->setStateFor(selfUniqueId, Common::Enums::PlayerState::STATE_NORMAL);
					}

					session->getPlayer().matchContext.matchStartTime = Main::Details::getUtcTimeMs();
					response.setCommand(request.getOrder(), 0, 38, room->getActualMap());
					response.setData(reinterpret_cast<const std::uint8_t*>(&selfUniqueId), sizeof(selfUniqueId));
					room->broadcastToRoom(response);
				}
				return true;
			}
			session->sendMessage("Error: not in a room");
			return false;
		}
	}
}

#endif
