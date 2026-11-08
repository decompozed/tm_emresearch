#ifndef HAVE_API_COMMAND_H
#define HAVE_API_COMMAND_H

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "ChatCommands/ICommand.h"
#include "ChatCommands/ChatCommands.h"
#include "Detail/IpcUtils.h"
#include "HaveApi/BattleMessages.h"
#include "HaveApi/HardpointZones.h"
#include "HaveApi/MessageWriter.h"
#include "HaveApi/Messages.h"
#include "HaveApi/PartyMatch.h"
#include "HaveApi/Sender.h"
#include "Managers/RoomsManager.h"
#include "Rooms/Room.h"
#include "MainServer.h"
#include "Utils/Logger.h"

namespace Main
{
	namespace Command
	{
		// /haveapi <message> [arguments]: sends one have_api message to the caller (tests of the messages that have
		// no game trigger yet). /haveapi alone lists the messages.
		struct HaveApi final : public ICommand
		{
			explicit HaveApi(const Common::Enums::PlayerGrade requiredGrade)
				: ICommand{ requiredGrade, "/haveapi [delay:<s>] <message> [arguments] | /haveapi endmatch | /haveapi lavapos | /haveapi lavahere [offset] | /haveapi zone <n> (/haveapi alone lists them)" }
			{
			}

			void execute(const std::string& command, std::shared_ptr<Main::Network::Session> session, MN::SessionsManager&, MC::RoomsManager& roomsManager,
				MP::MainScheduler&, std::uint32_t roomNumber, Main::MainServer& mainServer) override
			{
				std::istringstream words{ command };
				std::string ignoredName, subcommand;
				words >> ignoredName >> subcommand;
				// optional "delay:<seconds>" before the message name: send later (e.g. after opening a dialog)
				int delaySeconds = 0;
				if (subcommand.starts_with("delay:"))
				{
					const auto values = integers(subcommand.substr(6));
					delaySeconds = values.size() == 1 ? std::clamp(values[0], 0, 60) : 0;
					words >> subcommand;
				}
				std::string arguments;
				std::getline(words >> std::ws, arguments);

				if (subcommand == "endmatch")
				{
					endPartyMatch(*session, roomsManager, roomNumber);
					return;
				}
				if (subcommand == "reloadmap") // experiment: party map change without leaving the match
				{
					const auto values = integers(arguments);
					MC::Room* room = roomsManager.getRoomByNumber(roomNumber);
					Main::HaveApi::PartyMatch* match = room != nullptr ? room->haveApi().match() : nullptr;
					if (values.size() != 1 || match == nullptr)
					{
						session->sendMessage("/haveapi reloadmap <map id> (during a party match)");
						return;
					}
					session->sendMessage(match->reloadMap(static_cast<std::uint32_t>(values[0])) ? "map reload sent" : "map reload failed");
					return;
				}
				if (subcommand == "zone")
				{
					showZoneSpot(*session, roomsManager, roomNumber, arguments);
					return;
				}
				if (subcommand == "lavapos" || subcommand == "lavahere")
				{
					lavaCalibration(session, subcommand == "lavahere", arguments);
					return;
				}
				const auto found = std::ranges::find(subcommands(), subcommand, &Subcommand::name);
				if (found == subcommands().end())
				{
					for (const Subcommand& entry : subcommands())
					{
						session->sendMessage(std::string{ "/haveapi " } + std::string{ entry.name } + " " + std::string{ entry.usage });
					}
					return;
				}
				const auto uniqueIdBits = std::bit_cast<std::uint32_t>(session->getAccountInfo().uniqueId);
				const std::optional<std::vector<std::uint8_t>> message = found->encode(arguments, Common::HaveApi::playerIdFromUniqueId(uniqueIdBits));
				if (!message)
				{
					session->sendMessage(std::string{ "usage: /haveapi " } + std::string{ found->name } + " " + std::string{ found->usage });
					return;
				}
				if (delaySeconds > 0)
				{
					session->sendMessage("sending in " + std::to_string(delaySeconds) + " s");
					auto timer = std::make_shared<asio::steady_timer>(mainServer.getIoContext(), std::chrono::seconds{ delaySeconds });
					timer->async_wait([timer, weakSession = std::weak_ptr{ session }, data = *message](const asio::error_code& error) {
						if (const auto target = weakSession.lock(); target && !error)
						{
							Main::HaveApi::sendToPlayer(*target, data);
						}
					});
					return;
				}
				if (!Main::HaveApi::sendToPlayer(*session, *message))
				{
					session->sendMessage("error: the message could not be sent");
				}
			}

		private:
			// /haveapi endmatch: the room master ends the running party match now (result banner, then the match end).
			static void endPartyMatch(Main::Network::Session& session, MC::RoomsManager& roomsManager, std::uint32_t roomNumber)
			{
				Main::Classes::Room* room = roomsManager.getRoomByNumber(static_cast<std::uint16_t>(roomNumber));
				if (room == nullptr || room->haveApi().match() == nullptr)
				{
					session.sendMessage("no party match is running in this room");
					return;
				}
				if (!room->isHost(session.getAccountInfo().uniqueId))
				{
					session.sendMessage("only the room master can end the party match");
					return;
				}
				room->haveApi().match()->finishMatch();
			}

			// Lava Escape calibration (in a match of a SquareMode room). /haveapi lavapos: the caller's position as CastServer
			// sees it (order 281). /haveapi lavahere [offset]: also shows the lava floor, standing still, at the caller's
			// height + offset, to the caller only: the lava surface should be at the feet with offset 0.
			static void lavaCalibration(const std::shared_ptr<Main::Network::Session>& caller, bool show, const std::string& arguments)
			{
				Main::Ipc::M2C_partyLava(static_cast<std::uint32_t>(caller->getId()), { Common::HaveApi::LavaAction::Probe },
					[weak = std::weak_ptr<Main::Network::Session>{ caller }, show, arguments](std::optional<Common::HaveApi::LavaState> state) {
						if (const auto session = weak.lock())
						{
							showLavaCalibration(*session, state, show, arguments);
						}
					});
			}

			static void showLavaCalibration(Main::Network::Session& session, const std::optional<Common::HaveApi::LavaState>& state, bool show,
				const std::string& arguments)
			{
				if (!state || !state->active)
				{
					session.sendMessage("no position: be in a match of a party room (SquareMode) and move a little");
					return;
				}
				const std::string text = "position x " + std::to_string(state->wallX1) + " y " + std::to_string(state->wallY1) + " z " +
					std::to_string(state->floorZ);
				session.sendMessage(text);
				Utils::Logger::log("[HaveApi] " + std::string{ Main::HaveApi::playerName(session) } + " " + text);
				if (show)
				{
					const auto offset = integers(arguments);
					const float z = state->floorZ + static_cast<float>(offset.size() == 1 ? offset[0] : 0);
					Main::HaveApi::sendToPlayer(session, Common::HaveApi::encodeLavaFloor(true, Common::HaveApi::lavaFloorNodeHeight(z), 0.0f));
					session.sendMessage("lava surface shown at z " + std::to_string(z));
				}
			}

			// Hardpoint reachability check (in a Hardpoint match): /haveapi zone <n> shows spot n of the map's zone table
			// (Common/include/HaveApi/HardpointZones.h) to the caller only, as a white ring.
			static void showZoneSpot(Main::Network::Session& session, MC::RoomsManager& roomsManager, std::uint32_t roomNumber,
				const std::string& arguments)
			{
				Main::Classes::Room* room = roomsManager.getRoomByNumber(static_cast<std::uint16_t>(roomNumber));
				if (room == nullptr)
				{
					session.sendMessage("be in a room");
					return;
				}
				const std::uint32_t map = room->getActualMap();
				const auto spots = Common::HaveApi::findHardpointZones(map);
				const auto index = integers(arguments);
				if (spots.empty() || index.size() != 1 || index[0] < 1 || index[0] > static_cast<std::int32_t>(spots.size()))
				{
					session.sendMessage("map " + std::to_string(map) + ": " + std::to_string(spots.size()) + " zone spots, use /haveapi zone <1.." +
						std::to_string(spots.size()) + ">");
					return;
				}
				const auto& spot = spots[static_cast<std::size_t>(index[0] - 1)];
				Main::HaveApi::sendToPlayer(session, Common::HaveApi::encodeHardpointZone(true, spot.x, spot.y, 0));
				session.sendMessage("zone " + std::to_string(index[0]) + "/" + std::to_string(spots.size()) + " of map " + std::to_string(map) +
					" at x " + std::to_string(static_cast<int>(spot.x)) + " y " + std::to_string(static_cast<int>(spot.y)) + " (floor z " +
					std::to_string(static_cast<int>(spot.z)) + ")");
			}

			using Encoder = std::function<std::optional<std::vector<std::uint8_t>>(const std::string&, std::int32_t playerId)>;

			//NO_CHECK
			struct Subcommand
			{
				std::string_view name;
				std::string_view usage;
				Encoder encode;
			};

			[[nodiscard]] static std::vector<std::int32_t> integers(const std::string& text)
			{
				std::vector<std::int32_t> values;
				std::istringstream stream{ text };
				for (std::string word; stream >> word;)
				{
					std::int32_t value{};
					const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
					if (error != std::errc{} || end != word.data() + word.size())
					{
						return {};
					}
					values.push_back(value);
				}
				return values;
			}

			[[nodiscard]] static std::optional<std::vector<std::uint8_t>> text(Common::HaveApi::MessageId id, const std::string& value)
			{
				if (value.empty())
				{
					return std::nullopt;
				}
				return Common::HaveApi::encodeText(id, value);
			}

			[[nodiscard]] static const std::vector<Subcommand>& subcommands()
			{
				namespace CH = Common::HaveApi;
				static const std::vector<Subcommand> table = {
					{ "reset", "(0x03: reset the items of slots 11..16)",
						[](const std::string&, std::int32_t) { return std::optional{ CH::encodeEmpty(CH::MessageId::ResetSlotItems) }; } },
					{ "sound", "<file.wav> (0x04: 2D sound)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							if (args.empty()) return std::nullopt;
							// +0x28: 3D sound pointer, always 0 from the server; +0x2C: 2D sound name
							return CH::MessageWriter{ CH::MessageId::PlaySound }.append(std::uint32_t{ 0 }).appendText(args).finish();
						} },
					{ "profile", "<level> <xp> <next level xp> <total xp> <title>|<subtitle> (0x05, E_DLG_INFO open)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							std::istringstream stream{ args };
							std::string numbers[4];
							for (std::string& number : numbers) stream >> number;
							const auto values = integers(numbers[0] + " " + numbers[1] + " " + numbers[2] + " " + numbers[3]);
							std::string texts;
							std::getline(stream >> std::ws, texts);
							const std::size_t bar = texts.find('|');
							if (values.size() != 4 || bar == std::string::npos) return std::nullopt;
							CH::ProfilePayload profile{};
							const std::string title = texts.substr(0, bar), subtitle = texts.substr(bar + 1);
							std::copy_n(title.begin(), std::min(title.size(), profile.title.size() - 1), profile.title.begin());
							std::copy_n(subtitle.begin(), std::min(subtitle.size(), profile.subtitle.size() - 1), profile.subtitle.begin());
							profile.level = values[0];
							profile.experience = values[1];
							profile.nextLevelExperience = values[2];
							profile.totalExperience = values[3];
							return CH::encode(CH::MessageId::Profile, profile);
						} },
					{ "popup", "<dialog name> (0x07)", [](const std::string& args, std::int32_t) { return text(CH::MessageId::Popup, args); } },
					{ "inventory", "<capacity> <secondary capacity> (0x09)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.size() != 2) return std::nullopt;
							return CH::encode(CH::MessageId::InventoryCapacity, CH::InventoryCapacityPayload{ values[0], values[1] });
						} },
					{ "message", "<text> (0x20: message box)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							if (args.empty()) return std::nullopt;
							CH::CustomMessagePayload payload{};
							std::copy_n(args.begin(), std::min(args.size(), payload.text.size() - 1), payload.text.begin());
							return CH::encode(CH::MessageId::CustomMessage, payload);
						} },
					{ "music", "<file> (0x26)", [](const std::string& args, std::int32_t) { return text(CH::MessageId::PlayMusic, args); } },
					// party battle only: the DLL writes these into the battle dialog
					{ "points", "<points> [icon 1..12] (0x13, own row)",
						[](const std::string& args, std::int32_t playerId) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.empty() || values.size() > 2) return std::nullopt;
							return CH::encodePoints(playerId, values[0], values.size() == 2 ? values[1] : 0);
						} },
					{ "event", "<1..27> <seconds> [text] (0x16 announcement)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							std::istringstream stream{ args };
							std::string code, seconds, rest;
							stream >> code >> seconds;
							std::getline(stream >> std::ws, rest);
							const auto values = integers(code + " " + seconds);
							if (values.size() != 2 || values[0] < 1 || values[0] > 27) return std::nullopt;
							return CH::encodeAnnouncement(values[0], values[1], rest);
						} },
					{ "killnotice", "<seconds> (0x15)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.size() != 1) return std::nullopt;
							return CH::encodeValues(CH::MessageId::KillNotice, values[0]);
						} },
					{ "kills", "<n> (0x1C)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.size() != 1) return std::nullopt;
							return CH::encodeValues(CH::MessageId::Kills, values[0]);
						} },
					{ "deaths", "<n> (0x1D)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.size() != 1) return std::nullopt;
							return CH::encodeValues(CH::MessageId::Deaths, values[0]);
						} },
					{ "round", "<seconds> <banner> (0x10, neutral modifiers)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							std::istringstream stream{ args };
							std::string seconds, banner;
							stream >> seconds;
							std::getline(stream >> std::ws, banner);
							const auto values = integers(seconds);
							if (values.size() != 1 || banner.empty()) return std::nullopt;
							CH::RoundState round;
							round.banner = banner;
							round.lengthSeconds = static_cast<std::uint64_t>(values[0]);
							round.countdownSounds = values[0] != 0;
							return CH::encodeRoundState(round);
						} },
					{ "spectate", "<player id> [second value] (0x12: camera follows that player)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.empty() || values.size() > 2) return std::nullopt;
							return CH::encodeValues(CH::MessageId::Spectate, values[0], values.size() == 2 ? values[1] : 0);
						} },
					{ "lavafloor", "<show 0/1> <node z = surface - 20000> <speed> (0x23, Lava Escape match)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.size() != 3) return std::nullopt;
							return CH::encodeLavaFloor(values[0] != 0, static_cast<float>(values[1]), static_cast<float>(values[2]));
						} },
					{ "lavawalls", "<x1> <speed> <x2> <speed> <y1> <speed> <y2> <speed> (0x24, Lava Escape match, walls On)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto v = integers(args);
							if (v.size() != 8) return std::nullopt;
							auto f = [&](std::size_t i) { return static_cast<float>(v[i]); };
							return CH::encodeLavaWalls({ f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7) });
						} },
					{ "lavaphase", "<hot 0/1> <sleep 0/1> (0x25 icons)",
						[](const std::string& args, std::int32_t) -> std::optional<std::vector<std::uint8_t>> {
							const auto values = integers(args);
							if (values.size() != 2) return std::nullopt;
							return CH::encodeLavaPhase(values[0] != 0, values[1] != 0);
						} },
					{ "respawn", "(0x11: one-shot respawn)",
						[](const std::string&, std::int32_t) { return std::optional{ CH::encodeEmpty(CH::MessageId::RespawnOnce) }; } },
				};
				return table;
			}
		};

		REGISTER_CMD(HaveApi, Common::Enums::PlayerGrade::GRADE_TESTER)
	}
}

#endif
