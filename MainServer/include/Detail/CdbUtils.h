#ifndef CDB_UTILITY_H
#define CDB_UTILITY_H

#include <cstdint>
#include <optional>
#include <array>
#include <functional>
#include <chrono>

#include "MainEnums.h"
#include "ConstantDatabase/CdbSingleton.h"
#include "ConstantDatabase/Structures/CdbItemInfo.h"
#include "ConstantDatabase/Structures/CdbWeaponsInfo.h"
#include "ConstantDatabase/Structures/CdbUpgradeInfo.h"
#include "ConstantDatabase/Structures/CdbCapsuleInfo.h"
#include "ConstantDatabase/Structures/CdbCapsulePackageInfo.h"
#include "ConstantDatabase/Structures/CdbMapInfo.h"
#include "Structures/Capsule/CapsuleList.h"
#include "Detail/CapsuleRules.h"
#include "ConstantDatabase/Structures/CdbRewardInfo.h"
#include "ConstantDatabase/Structures/CdbGradeInfo.h"
#include "ConstantDatabase/Structures/CdbVendor.h"
#include "ConstantDatabase/Structures/CdbEffectInfo.h"
#include <random>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include "Enums/RoomEnums.h"
#include <ConstantDatabase/Structures/CdbMissionEventInfo.h>
#include "Utils/SetupParser.h"
#include "Detail/UpgradeResetRules.h"

namespace Main
{
	namespace CdbUtils
	{
		template<typename T>
		using Cdb = Common::ConstantDatabase::Cdb<T>;

		using cdbItemWeapons = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbItemWeapon>;
		using cdbUpgrades = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbUpgradeInfo>;
		using cdbCapsuleInfos = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbCapsuleInfo>;
		using cdbCapsulePackageInfos = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbCapsulePackageInfo>;
		using cdbMapInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbMapInfo>;
		using cdbRewardInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbRewardInfo>;
		using ItemTypePricePair = std::pair<Main::Enums::ItemCurrencyType, std::uint32_t>;
		using cdbLevelInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbGradeInfo>;
		using itemPackageInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbItemPackageInfo>;
		using VendorInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbVendorInfo>;
		using EffectInfo = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbEffectInfo>;

		inline constexpr std::uint32_t bigMapPrototypeId = 58;

		inline std::uint32_t getRandomMapForMode(std::uint32_t modeId)
		{
			std::vector<std::uint32_t> retrievedMapIdsForMode;
			for (const auto& [entryId, mapInfo] : cdbMapInfo::getInstance().getEntries())
			{
				if (mapInfo.mi_id == bigMapPrototypeId)
				{
					continue;
				}
				if ((modeId == Common::Enums::TeamDeathMatch || modeId == Common::Enums::Clan_TeamDeathMatch) && mapInfo.mi_mod_tdm
					|| modeId == Common::Enums::FreeForAll && mapInfo.mi_mod_ffa
					|| modeId == Common::Enums::ItemMatch && mapInfo.mi_mod_itm
					|| (modeId == Common::Enums::CaptureTheBattery || modeId == Common::Enums::Clan_CaptureTheBattery) && mapInfo.mi_mod_ctf
					|| modeId == Common::Enums::CloseCombat && mapInfo.mi_mod_ctm
					|| (modeId == Common::Enums::Elimination || modeId == Common::Enums::Clan_Elimination) && mapInfo.mi_mod_sab
					|| modeId == Common::Enums::SuperItemMatch && mapInfo.mi_mod_cim
					|| modeId == Common::Enums::ZombieMode && mapInfo.mi_mod_zsm
					|| modeId == Common::Enums::ArmsRace && mapInfo.mi_mod_grm
					|| modeId == Common::Enums::Scrimmage && mapInfo.mi_mod_grm
					|| (modeId == Common::Enums::BombBattle || modeId == Common::Enums::Clan_BombBattle) && mapInfo.mi_mod_bmb
					|| modeId == Common::Enums::SniperMode && mapInfo.mi_mod_sni
					|| modeId == Common::Enums::SquareMode && mapInfo.mi_mod_nod
					|| modeId == Common::Enums::BossBattle && mapInfo.mi_mod_pve
					|| modeId == Common::Enums::AiBattle && mapInfo.mi_mod_bot)
				{
					retrievedMapIdsForMode.push_back(mapInfo.mi_id);
				}
			}

			if (retrievedMapIdsForMode.empty())
			{
				return 6; // default map: bitmap
			}

			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<std::size_t> dist(0, retrievedMapIdsForMode.size() - 1);

			return retrievedMapIdsForMode[dist(gen)];
		}

		inline std::optional<ItemTypePricePair> getItemPrice(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
			{
				if (entry->ii_buy_coupon)
					return std::pair{ Main::Enums::ItemCurrencyType::ITEM_COUPON, entry->ii_buy_coupon };
				else if (entry->ii_buy_cash)
					return std::pair{ Main::Enums::ItemCurrencyType::ITEM_RT, entry->ii_buy_cash };
				else if (entry->ii_buy_point)
					return std::pair{ Main::Enums::ItemCurrencyType::ITEM_MP, entry->ii_buy_point };
			}
			return std::nullopt;
		}

		// ei_key == three ==> HP
		// ei_key == thirteen ==> Speed.
		inline std::pair<std::uint32_t, std::uint32_t> getExpAndMpEnhancementFor(std::uint32_t itemId)
		{
			std::pair<std::uint32_t, std::uint32_t> totalExpMpPercentageToAdd{};

			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
			{
				const std::array<std::uint32_t, 3> effects = { entry->ii_effect_1, entry->ii_effect_2, entry->ii_effect_3 };
				for (auto effectId : effects)
				{
					if (const auto effect = EffectInfo::getInstance().getEntry(effectId))
					{
						if (effect->ei_valueA >= 10 && effect->ei_valueA <= 1000)
						{
							if (effect->ei_key == 122) // exp
							{
								totalExpMpPercentageToAdd.first += effect->ei_valueA / 10;
							}
							else if (effect->ei_key == 123) // mp
							{
								totalExpMpPercentageToAdd.second += effect->ei_valueA / 10;
							}
						}
					}
				}
			}

			return totalExpMpPercentageToAdd;
		}

		inline bool isNoOptionItem(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
			{
				std::string optionStr(entry->ii_option.data(), strnlen(entry->ii_option.data(), entry->ii_option.size()));
				return optionStr.find("No option") != std::string::npos;
			}
			return false;
		}

		inline std::optional<std::uint16_t> getItemDurability(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
				return entry->ii_durable_value;
			return std::nullopt;
		}

		inline std::optional<std::uint16_t> getItemStocks(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
				return entry->ii_stocks;
			return std::nullopt;
		}

		inline std::optional<std::string> getItemName(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
				return std::string{ entry->ii_name.begin(), entry->ii_name.end() };
			return std::nullopt;
		}

		inline std::optional<bool> isImmediatelySet(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
			{
				return entry->ii_immediately_set;
			}
			return std::nullopt;
		}

		/**
		* @return The duration of the given itemId. If the item is not found, returns 3, which signals that 
		* the item is expired to the game client so that it cannot be used further (e.g. prevent exploits)
		* Note: usually, the caller should do a sanity check such as: expirationDate = duration <= 3 ? duration : serialInfo.itemCreationDate + duration
		*/
		inline std::uint32_t getItemDuration(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
			{
				return entry->ii_limited_time;
			}
			return 3; // expired (TODO: remove magic number)
		}

		inline bool itemExists(std::uint32_t itemId)
		{
			return getItemDuration(itemId) != 3;
		}

		// have_api option swap: the set parts / sets come as a "Run speed +x%" item and an "HP +y" twin (iteminfo
		// ii_name_option), the same item otherwise (name, mesh, type, characters, grade, duration) with the id next to
		// it (almost always the pair 2k / 2k + 1; a few S, H, S triplets: the third Speed goes to the HP, which comes
		// back to the first, the same stats and price). "No option" items and the ones without a twin do not swap.
		enum class ItemOption : std::uint8_t { None = 0, Speed = 1, Health = 2 };

		inline ItemOption itemOptionOf(const Common::ConstantDatabase::CdbItemInfo& info)
		{
			std::string option(info.ii_name_option.data(), strnlen(info.ii_name_option.data(), info.ii_name_option.size()));
			std::ranges::transform(option, option.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (option.starts_with("run speed")) return ItemOption::Speed;
			if (option.starts_with("hp +")) return ItemOption::Health;
			return ItemOption::None;
		}

		inline ItemOption getItemOption(std::uint32_t itemId)
		{
			const auto* info = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbItemInfo>::getInstance().getEntry(itemId);
			return info != nullptr ? itemOptionOf(*info) : ItemOption::None;
		}

		inline std::optional<std::uint32_t> getItemOptionCounterpart(std::uint32_t itemId)
		{
			const auto& items = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbItemInfo>::getInstance();
			const auto* info = items.getEntry(itemId);
			if (info == nullptr) return std::nullopt;
			const ItemOption option = itemOptionOf(*info);
			if (option == ItemOption::None) return std::nullopt;
			const ItemOption wanted = option == ItemOption::Speed ? ItemOption::Health : ItemOption::Speed;
			const auto text = [](const auto& field) { return std::string_view(field.data(), strnlen(field.data(), field.size())); };
			const auto sameItem = [&](const Common::ConstantDatabase::CdbItemInfo& other) {
				return text(other.ii_name) == text(info->ii_name) && text(other.ii_meshfilename) == text(info->ii_meshfilename)
					&& other.ii_type == info->ii_type && other.ii_grade == info->ii_grade && other.ii_limited_time == info->ii_limited_time
					&& other.ii_class_a == info->ii_class_a && other.ii_class_b == info->ii_class_b && other.ii_class_c == info->ii_class_c
					&& other.ii_class_d == info->ii_class_d && other.ii_class_e == info->ii_class_e && other.ii_class_f == info->ii_class_f
					&& other.ii_class_g == info->ii_class_g && other.ii_class_h == info->ii_class_h && other.ii_class_i == info->ii_class_i
					&& other.ii_class_j == info->ii_class_j && other.ii_class_k == info->ii_class_k && other.ii_class_l == info->ii_class_l
					&& other.ii_class_m == info->ii_class_m && other.ii_class_n == info->ii_class_n && other.ii_class_o == info->ii_class_o
					&& other.ii_class_p == info->ii_class_p;
			};
			const std::uint32_t pair = itemId ^ 1u; // the same pair first, then the other neighbour
			for (const std::uint32_t candidate : { pair, pair == itemId + 1 ? itemId - 1 : itemId + 1 })
			{
				const auto* other = items.getEntry(candidate);
				if (other != nullptr && itemOptionOf(*other) == wanted && sameItem(*other)) return candidate;
			}
			return std::nullopt;
		}

		inline std::optional<std::uint32_t> getItemType(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
			{
				return entry->ii_type;
			}
			return std::nullopt;
		}

		inline std::optional<bool> isTradeable(std::uint32_t itemId)
		{
			if (const auto entry = cdbItemWeapons::getInstance().getEntry(itemId); entry)
			{
				return entry->ii_tradeable && (entry->ii_type_inven >= 1 && entry->ii_type_inven <= 21) && entry->ii_type_inven != 10;
			}
			return std::nullopt;
		}

		inline bool isUpgradeResetItem(std::uint32_t itemId)
		{
			const auto* info = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbItemInfo>::getInstance().getEntry(itemId);
			if (!info) return false;
			return Main::Details::hasUpgradeResetEffect({ info->ef_effect_1, info->ef_effect_2, info->ef_effect_3 },
				[](std::uint32_t effectId) -> std::optional<std::uint32_t>
				{
					if (const auto effect = EffectInfo::getInstance().getEntry(effectId)) return effect->ei_key;
					return std::nullopt;
				});
		}

		inline std::optional<std::uint32_t> getBaseItemId(std::uint32_t itemId)
		{
			std::uint32_t currentId = itemId;
			while (true)
			{
				const auto entry = cdbUpgrades::getInstance().getEntry(currentId);
				if (!entry)
					return std::nullopt;
				if (entry->ui_parentid == 0)
					return entry->ui_itemid;
				currentId = entry->ui_parentid;
			}
		}

		inline std::vector<Main::Structures::CapsuleList> getCapsuleEvents(const Main::Structures::CapsuleListDatabase& sale, std::uint64_t now)
		{
			std::vector<Main::Structures::CapsuleList> ret;
			for (const auto& [id, capsuleInfoStruct] : cdbCapsuleInfos::getInstance().getEntries())
			{
				ret.push_back(Details::capsuleListEntry(static_cast<std::uint32_t>(capsuleInfoStruct.gi_id), capsuleInfoStruct.gi_type,
					capsuleInfoStruct.gi_price, capsuleInfoStruct.gi_luckypoint, sale, now));
			}
			return ret;
		}

		inline std::optional<Common::ConstantDatabase::CdbCapsuleInfo> getCapsuleInfoById(std::uint32_t gi_id)
		{
			for (const auto& [unused, capsuleInfoStruct] : cdbCapsuleInfos::getInstance().getEntries())
			{
				if (capsuleInfoStruct.gi_id == gi_id) return capsuleInfoStruct;
			}
			return std::nullopt;
		}

		inline std::optional<std::uint32_t> getEventMissionRewardFor(std::uint32_t em_id)
		{
			using evetMission = Common::ConstantDatabase::CdbSingleton<Common::ConstantDatabase::CdbEventMissionInfo>;
			if (const auto entry = evetMission::getInstance().getEntry(em_id); entry)
			{
				return entry->em_rewardid;
			}
			return std::nullopt;
		}

		inline std::array<double, 3> getAverageSpinCostByCurrency()
		{
			std::array<double, 3> averageCosts{}; // [0]=coin, [1]=rt, [2]=mp, [3]=lucky spin
			std::array<double, 3> totalCapsuleTypesByCurrency{};
			for (std::size_t total = 0; const auto & [id, capsuleInfoStruct] : cdbCapsuleInfos::getInstance().getEntries())
			{
				if (capsuleInfoStruct.gi_type >= 3) continue;
				averageCosts[capsuleInfoStruct.gi_type] += capsuleInfoStruct.gi_price;
				++totalCapsuleTypesByCurrency[capsuleInfoStruct.gi_type];
			}
			averageCosts[0] /= totalCapsuleTypesByCurrency[0];
			averageCosts[1] /= totalCapsuleTypesByCurrency[1];
			averageCosts[2] /= totalCapsuleTypesByCurrency[2];

			return averageCosts;
		}

		template<typename T>
		inline std::optional<std::vector<T>> getAllEntries(std::uint32_t id)
			requires std::same_as<T, Common::ConstantDatabase::CdbCapsulePackageInfo> or std::same_as<T, Common::ConstantDatabase::CdbItemPackageInfo>
		{
			using CdbType = std::conditional_t<std::same_as<T, Common::ConstantDatabase::CdbCapsulePackageInfo>, cdbCapsulePackageInfos, itemPackageInfo>;
			if (const auto entries = CdbType::getInstance().getEntriesFor(id); entries)
			{
				return *entries;
			}
			return std::nullopt;
		}

		inline std::vector<Common::ConstantDatabase::CdbCapsulePackageInfo> getAllRareCapsuleItems()
		{
			std::vector<Common::ConstantDatabase::CdbCapsulePackageInfo> rareItems;
			const auto& capsuleInfos = cdbCapsuleInfos::getInstance().getEntries();

			for (const auto& [unused, capsuleInfo] : capsuleInfos)
			{
				if (auto relatedPackages = cdbCapsulePackageInfos::getInstance().getEntriesFor(capsuleInfo.gi_infoid))
				{
					for (const auto& packageInfo : relatedPackages.value())
					{
						if (packageInfo.gi_type == 1)
							rareItems.push_back(packageInfo);
					}
				}
			}
			return rareItems;
		}
	}
}


#endif