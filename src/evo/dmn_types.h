// Copyright (c) 2023 The BiblePay Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_DMN_TYPES_H
#define BITCOIN_EVO_DMN_TYPES_H

#include <amount.h>

#include <limits>
#include <string_view>

enum class MnType : uint16_t {
    Regular = 0,
    Temple = 1,
    Altar = 2,
    COUNT,
    Invalid = std::numeric_limits<uint16_t>::max(),
};

template<typename T> struct is_serializable_enum;
template<> struct is_serializable_enum<MnType> : std::true_type {};

namespace dmn_types {

struct mntype_struct
{
    const int32_t voting_weight;
    const CAmount collat_amount;
    const std::string_view description;
};

constexpr auto Regular = mntype_struct{
    .voting_weight = 10,
    .collat_amount = 4500001 * COIN,
    .description = "Regular",
};
constexpr auto Temple = mntype_struct{
    .voting_weight = 100,
    .collat_amount = 45000001 * COIN,
    .description = "Temple",
};
constexpr auto Altar = mntype_struct{
    .voting_weight = 1,
    .collat_amount = 450001 * COIN,
    .description = "Altar",
};
constexpr auto Invalid = mntype_struct{
    .voting_weight = 0,
    .collat_amount = MAX_MONEY,
    .description = "Invalid",
};

[[nodiscard]] static constexpr bool IsCollateralAmount(CAmount amount)
{
    return amount == Regular.collat_amount ||
           amount == Temple.collat_amount || amount == Altar.collat_amount;
}

} // namespace dmn_types

[[nodiscard]] constexpr const dmn_types::mntype_struct GetMnType(MnType type)
{
    switch (type) {
        case MnType::Regular: return dmn_types::Regular;
        case MnType::Temple: return dmn_types::Temple;
        default: return dmn_types::Invalid;
    }
}

[[nodiscard]] constexpr const bool IsValidMnType(MnType type)
{
    return type < MnType::COUNT;
}

#endif // BITCOIN_EVO_DMN_TYPES_H
