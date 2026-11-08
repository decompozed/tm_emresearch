#ifndef UPGRADE_RESET_RULES_H
#define UPGRADE_RESET_RULES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace Main
{
	namespace Details
	{
		// effectinfo ei_key of the Upgrade Reset item (4305008, effect 15048)
		inline constexpr std::uint32_t upgradeResetEffectKey = 156;

		template<typename EffectKey>
		bool hasUpgradeResetEffect(const std::array<int, 3>& effects, EffectKey&& effectKey)
		{
			return std::ranges::any_of(effects, [&](int effectId)
				{
					const std::optional<std::uint32_t> key = effectKey(static_cast<std::uint32_t>(effectId));
					return key && *key == upgradeResetEffectKey;
				});
		}
	}
}

#endif
