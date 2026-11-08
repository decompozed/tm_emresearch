#ifndef TRADE_FINALIZE_HANDLER_H
#define TRADE_FINALIZE_HANDLER_H

#include "Network/Sessions/MainSession.h"
#include "Network/MainSessionManager.h"
#include "Enums/GameEnums.h"
#include "Network/Packet.h"
#include <vector>
#include <cstdint>
#include <optional>
#include <array>
#include "Structures/TradeSystem/TradeAck.h"
#include "Detail/TradeRules.h"
#include "Utils/Logger.h"
#include "Macros.h"
#include <format>
#include <string>

namespace Main
{
	namespace Handlers
	{
PACK_PUSH(1)
struct TradeUnusedFinalItem
		{
			char unused[8]{};
			std::uint32_t totalNewMp{}; // This was used in the old trade system
			std::uint32_t unusedTotal{};
			std::array<Main::Structures::ItemSerialInfo, 10> itemSerialInfosUnused{};
			std::uint32_t unusedTotal1{};
			std::array<std::uint32_t, 10> itemIdsUnused{};
		};
PACK_POP()

		inline std::optional<std::uint32_t> tradeableItemId(Main::Network::Session& session, std::uint32_t itemNumber)
		{
			Main::Structures::ItemSerialInfo serialInfo;
			serialInfo.itemNumber = itemNumber;
			const auto& inventory = session.getPlayer().inventory;
			const auto itemId = inventory.findItemIdBySerialInfo(serialInfo);
			if (!itemId || !inventory.isItemTradeable(serialInfo) || !session.canTradeItem(itemNumber)) return std::nullopt;
			return itemId;
		}

		// Order 198 extra 1 (0xB03D40): MSG_TRADE_CANCEL for the account in the data, CMsgTradingCancle closes E_DLG_TRADE
		inline void abortTrade(const Common::Network::Packet& request, const std::shared_ptr<Main::Network::Session>& session,
			const std::shared_ptr<Main::Network::Session>& targetSession, const std::string& reason)
		{
			Common::Network::Packet response;
			response.setTcpHeader(request.getSession(), Common::Enums::USER_LARGE_ENCRYPTION);
			response.setOrder(198);
			response.setExtra(Main::Enums::TradeSystemExtra::TRADE_CANCELLED);
			const auto notify = [&](const std::shared_ptr<Main::Network::Session>& to, const std::shared_ptr<Main::Network::Session>& partner)
				{
					const Main::Structures::TradeAck ack{ Main::Structures::UniqueId{}, partner->getAccountInfo().accountID };
					response.setData(reinterpret_cast<const std::uint8_t*>(&ack), sizeof(ack));
					to->asyncWrite(response);
					to->sendMessage(reason);
					to->resetTradeInfo();
				};
			notify(session, targetSession);
			notify(targetSession, session);
		}

		inline void handleTradeFinalization(const Common::Network::Packet& request, std::shared_ptr<Main::Network::Session> session, 
			Main::Network::SessionsManager& sessionsManager)
		{
			Common::Network::Packet response;
			response.setTcpHeader(request.getSession(), Common::Enums::USER_LARGE_ENCRYPTION);
			response.setOrder(request.getOrder());
			auto targetSession = sessionsManager.getSessionByAccountId(session->getPlayer().tradeInfo.getCurrentlyTradingWithAccountId());
			if (targetSession && targetSession->getPlayer().tradeInfo.getCurrentlyTradingWithAccountId() != session->getAccountInfo().accountID)
			{
				targetSession = nullptr;
			}
			if (targetSession)
			{
				if (targetSession->getPlayer().tradeInfo.hasPlayerLocked())
				{
					session->setPlayerState(Common::Enums::PlayerState::STATE_INVENTORY);
					targetSession->setPlayerState(Common::Enums::PlayerState::STATE_INVENTORY);
					// The other player has confirmed the trade, we can proceed
					response.setOrder(199);

					auto selfTradedItems = session->getPlayer().tradeInfo.getTradedItems();
					auto targetTradedItems = targetSession->getPlayer().tradeInfo.getTradedItems();

					if (!targetSession->getPlayer().inventory.hasEnoughInventorySpace(selfTradedItems.size())
						|| !session->getPlayer().inventory.hasEnoughInventorySpace(targetTradedItems.size()))
					{
						abortTrade(request, session, targetSession, "Trade cancelled: not enough inventory space.");
						return;
					}

					const auto selfMicroPoints = Details::microPointsAfterTradeFee(session->getAccountInfo().microPoints, selfTradedItems.size());
					const auto targetMicroPoints = Details::microPointsAfterTradeFee(targetSession->getAccountInfo().microPoints, targetTradedItems.size());
					const bool selfOfferHeld = Details::offerStillHeld(selfTradedItems,
						[&](std::uint32_t itemNumber) { return tradeableItemId(*session, itemNumber); });
					const bool targetOfferHeld = Details::offerStillHeld(targetTradedItems,
						[&](std::uint32_t itemNumber) { return tradeableItemId(*targetSession, itemNumber); });
					if (!selfMicroPoints || !targetMicroPoints || !selfOfferHeld || !targetOfferHeld)
					{
						const auto logRefusal = [](const Main::Network::Session& player, bool microPointsOk, bool offerHeld)
							{
								if (microPointsOk && offerHeld) return;
								::Utils::Logger::log(std::format("[AntiCheat] account {} trade refused at finalization:{}{}",
									static_cast<std::uint32_t>(player.getAccountInfo().accountID), microPointsOk ? "" : " not enough MP for the fee",
									offerHeld ? "" : " an offered item is no longer held unchanged"), ::Utils::LogType::Warning);
							};
						logRefusal(*session, selfMicroPoints.has_value(), selfOfferHeld);
						logRefusal(*targetSession, targetMicroPoints.has_value(), targetOfferHeld);
						abortTrade(request, session, targetSession, "Trade cancelled: an offered item changed or the Micro Points do not cover the trade fee.");
						return;
					}

					std::uint64_t targetLatestItemNumber = targetSession->getPlayer().inventory.getLatestItemNumber();
					std::uint64_t selfLatestItemNumber = session->getPlayer().inventory.getLatestItemNumber();

					// Self
					TradeUnusedFinalItem unusedFinalTradeItem;
					session->setAccountMicroPoints(*selfMicroPoints);
					session->sendCurrency();
					unusedFinalTradeItem.totalNewMp = session->getAccountInfo().microPoints;
					response.setData(reinterpret_cast<std::uint8_t*>(&unusedFinalTradeItem), sizeof(unusedFinalTradeItem));
					for (auto& current : selfTradedItems)
					{
						current.itemSerialInfo.itemNumber = ++targetLatestItemNumber;
					}
					targetSession->getPlayer().inventory.setLatestItemNumber(targetLatestItemNumber);
					session->asyncWrite(response);

					// Target
					targetSession->setAccountMicroPoints(*targetMicroPoints);
					targetSession->sendCurrency();
					unusedFinalTradeItem.totalNewMp = targetSession->getAccountInfo().microPoints;
					response.setData(reinterpret_cast<std::uint8_t*>(&unusedFinalTradeItem), sizeof(unusedFinalTradeItem));
					for (auto& current : targetTradedItems)
					{
						current.itemSerialInfo.itemNumber = ++selfLatestItemNumber;
					}
					session->getPlayer().inventory.setLatestItemNumber(selfLatestItemNumber);
					targetSession->asyncWrite(response);
					session->deleteItems(session->getPlayer().tradeInfo.getTradedItems(), "Item deleted after it was traded to " + std::string{targetSession->getPlayer().accountInfo.nickname}
						+ " (target AID: " + std::to_string(targetSession->getAccountInfo().accountID) + ")");
					session->resetTradeInfo();

					targetSession->deleteItems(targetSession->getPlayer().tradeInfo.getTradedItems(), "Item deleted after it was traded to " + std::string{ session->getPlayer().accountInfo.nickname }
						+ " (target AID: " + std::to_string(session->getAccountInfo().accountID) + ")");
					targetSession->spawnItems(selfTradedItems, "Item received from trade, from user " + std::string{ session->getPlayer().accountInfo.nickname }
					+ " (AID: " + std::to_string(session->getAccountInfo().accountID) + ")");
					session->spawnItems(targetTradedItems, "Item received from trade, from user " + std::string{ targetSession->getPlayer().accountInfo.nickname }
					    + " (AID: " + std::to_string(targetSession->getAccountInfo().accountID) + ")");
					targetSession->resetTradeInfo();

				}
				else if (session->getPlayer().tradeInfo.hasPlayerLocked())
				{
					// The player has already clicked "Confirm", but tries to click "Confirm" again ==> Tell them that both players must confirm in this case
					response.setExtra(Main::Enums::TradeSystemExtra::BOTH_PLAYERS_MUST_CONFIRM_TRADE_BEFORE_FINALIZATION);
					Main::Structures::TradeAck ack{ Main::Structures::UniqueId{}, session->getAccountInfo().accountID };
					response.setData(reinterpret_cast<std::uint8_t*>(&ack), sizeof(ack));
					session->asyncWrite(response);
					return;
				}
				else
				{
					// The other player has not yet confirmed the trade and neither did we, notify the other player that we've confirmed the trade now
					session->getPlayer().tradeInfo.lockTrade();
					response.setOrder(197);
					response.setExtra(Main::Enums::TradeSystemExtra::TRADE_CONFIRMED_NOTIFY_OTHER_PLAYER);
					targetSession->asyncWrite(response);
				}
			}
			else
			{
				response.setOrder(198);
				response.setExtra(Enums::TradeSystemExtra::CANNOT_TRADE_NOW_OR_PLAYER_OFFLINE);
				session->asyncWrite(response);
				session->resetTradeInfo();
				session->setPlayerState(Common::Enums::PlayerState::STATE_INVENTORY);
			}
		}
	}
}

#endif