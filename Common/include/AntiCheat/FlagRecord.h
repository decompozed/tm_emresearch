#ifndef AC_FLAG_RECORD_H
#define AC_FLAG_RECORD_H

#include "Event.h"
#include <string>
#include <string_view>

namespace Ac
{
    inline constexpr std::string_view createFlagsTableSql =
        "CREATE TABLE IF NOT EXISTS CheatFlags ("
        "ID INT AUTO_INCREMENT PRIMARY KEY, "
        "Description TEXT NOT NULL, "
        "Timestamp DATETIME NOT NULL"
        ")";

    inline constexpr std::string_view insertFlagSql = "INSERT INTO CheatFlags (Description, Timestamp) VALUES (?, NOW())";

    inline std::string describeFlag(const ACFlag& flag)
    {
        return "CheatType: " + flag.cheatType + ", AccountID: " + std::to_string(flag.accountId) + ", Details: " + flag.details;
    }
}

#endif
