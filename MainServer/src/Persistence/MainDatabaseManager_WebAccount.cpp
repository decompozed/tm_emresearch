// Database access of the website's account API. Prepared statements only.

#include <memory>
#include <optional>
#include <string>

#include "Structures/AccountInfo/MainAccountInfo.h"
#include "Structures/Item/MainItem.h"
#include "Structures/Item/MainBoughtItem.h"
#include "Structures/Item/MainEquippedItem.h"
#include "Structures/Events/MainEventsList.h"
#include "Structures/PlayerLists/Friend.h"
#include "Structures/PlayerLists/BlockedPlayer.h"
#include "Structures/Mailbox/Mailbox.h"
#include "Structures/AccountInfo/MuteInfo.h"
#include "Persistence/MainDatabaseManager.h"
#include "MainEnums.h"
#include "Utils/Logger.h"
#include <mariadb/conncpp.hpp>
#include <mariadb/conncpp/Driver.hpp>
#include <mariadb/conncpp/Connection.hpp>

namespace Main
{
    namespace Persistence
    {
        namespace
        {
            constexpr int duplicateEntryError = 1062; // ER_DUP_ENTRY

            Main::Structures::WebAccountLookup lookup(sql::Connection* con, const char* whereClause, const std::string* text, std::uint32_t id, const char* caller)
            {
                Main::Structures::WebAccountLookup result;
                try
                {
                    const std::string query = std::string("SELECT AccountID, Username, Nickname, Password, Secret, SuspendedUntil, Grade, TwoFactorEnabled, "
                        "TotpLastStep, TwoFactorLockedUntil, Level, Kills, Deaths, Wins, Loses, Playtime, WebSessionVersion FROM Users WHERE ") + whereClause;
                    std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(query));
                    if (text)
                    {
                        stmt->setString(1, *text);
                    }
                    else
                    {
                        stmt->setUInt(1, id);
                    }
                    std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
                    if (!res->next())
                    {
                        result.status = Main::Structures::WebAccountLookup::Status::NotFound;
                        return result;
                    }
                    auto& a = result.account;
                    a.accountId = static_cast<std::uint32_t>(res->getInt("AccountID"));
                    a.username = res->getString("Username").c_str();
                    a.nickname = res->getString("Nickname").c_str();
                    a.passwordHash = res->isNull("Password") ? "" : res->getString("Password").c_str();
                    a.encryptedSecret = res->isNull("Secret") ? "" : res->getString("Secret").c_str();
                    a.suspendedUntil = res->isNull("SuspendedUntil") ? "" : res->getString("SuspendedUntil").c_str();
                    a.grade = res->getInt("Grade");
                    a.twoFactorEnabled = res->getInt("TwoFactorEnabled") != 0;
                    a.totpLastStep = res->getUInt64("TotpLastStep");
                    a.twoFactorLockedUntil = res->getUInt64("TwoFactorLockedUntil");
                    a.level = res->getInt64("Level");
                    a.kills = res->getInt64("Kills");
                    a.deaths = res->getInt64("Deaths");
                    a.wins = res->getInt64("Wins");
                    a.loses = res->getInt64("Loses");
                    a.playtime = res->getInt64("Playtime");
                    a.webSessionVersion = res->getUInt("WebSessionVersion");
                    result.status = Main::Structures::WebAccountLookup::Status::Found;
                }
                catch (const sql::SQLException& e)
                {
                    ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, caller);
                    result.status = Main::Structures::WebAccountLookup::Status::Error;
                }
                return result;
            }
        }

        Main::Structures::WebAccountLookup PersistentDatabase::getWebAccountByUsername(const std::string& username)
        {
            return lookup(m_con.get(), "Username = ?", &username, 0, "PersistentDatabase::getWebAccountByUsername");
        }

        Main::Structures::WebAccountLookup PersistentDatabase::getWebAccountById(std::uint32_t accountId)
        {
            return lookup(m_con.get(), "AccountID = ?", nullptr, accountId, "PersistentDatabase::getWebAccountById");
        }

        // Same row as AddPlayer: the table defaults give the normal grade and the starting values. The unique keys decide a race
        // between two registrations of the same name, so there is no check-then-insert gap.
        Main::Structures::WebRegisterResult PersistentDatabase::registerWebAccount(const std::string& username, const std::string& passwordHash,
            const std::string& nickname, const std::string& encryptedEmail)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement(
                    "INSERT INTO Users (Username, Password, Nickname, Email, Secret) VALUES (?, ?, ?, ?, '')"));
                stmt->setString(1, username);
                stmt->setString(2, passwordHash);
                stmt->setString(3, nickname);
                stmt->setString(4, encryptedEmail);
                return stmt->executeUpdate() == 1 ? Main::Structures::WebRegisterResult::Created : Main::Structures::WebRegisterResult::Error;
            }
            catch (sql::SQLException& e)
            {
                if (e.getErrorCode() == duplicateEntryError)
                {
                    const std::string message = e.what();
                    if (message.find("uniqueNickname") != std::string::npos)
                    {
                        return Main::Structures::WebRegisterResult::NicknameTaken;
                    }
                    if (message.find("uniqueUsername") != std::string::npos)
                    {
                        return Main::Structures::WebRegisterResult::UsernameTaken;
                    }
                }
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::registerWebAccount");
                return Main::Structures::WebRegisterResult::Error;
            }
        }

        // The WHERE clause on the old hash makes it a compare-and-set: it only replaces the hash that was just verified
        bool PersistentDatabase::replacePasswordHash(std::uint32_t accountId, const std::string& oldHash, const std::string& newHash)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement("UPDATE Users SET Password = ? WHERE AccountID = ? AND Password = ?"));
                stmt->setString(1, newHash);
                stmt->setUInt(2, accountId);
                stmt->setString(3, oldHash);
                return stmt->executeUpdate() > 0;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::replacePasswordHash");
                return false;
            }
        }

        // A password change from the website: same compare-and-set as above, and in the same statement the session version
        // moves on, so every website token issued before the change stops working
        bool PersistentDatabase::changeWebPassword(std::uint32_t accountId, const std::string& oldHash, const std::string& newHash)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement(
                    "UPDATE Users SET Password = ?, WebSessionVersion = WebSessionVersion + 1 WHERE AccountID = ? AND Password = ?"));
                stmt->setString(1, newHash);
                stmt->setUInt(2, accountId);
                stmt->setString(3, oldHash);
                return stmt->executeUpdate() > 0;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::changeWebPassword");
                return false;
            }
        }

        // Logout: ends every website session of the account. Only from the version the token carried, so a replayed logout
        // with an old token changes nothing.
        bool PersistentDatabase::endWebSessions(std::uint32_t accountId, std::uint32_t currentVersion)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement(
                    "UPDATE Users SET WebSessionVersion = WebSessionVersion + 1 WHERE AccountID = ? AND WebSessionVersion = ?"));
                stmt->setUInt(1, accountId);
                stmt->setUInt(2, currentVersion);
                return stmt->executeUpdate() > 0;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::endWebSessions");
                return false;
            }
        }

        std::optional<std::uint32_t> PersistentDatabase::incrementFailed2fas(std::uint32_t accountId)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> update(m_con->prepareStatement("UPDATE Users SET Failed2faCount = Failed2faCount + 1 WHERE AccountID = ?"));
                update->setUInt(1, accountId);
                if (update->executeUpdate() == 0)
                {
                    return std::nullopt;
                }
                std::unique_ptr<sql::PreparedStatement> select(m_con->prepareStatement("SELECT Failed2faCount FROM Users WHERE AccountID = ?"));
                select->setUInt(1, accountId);
                std::unique_ptr<sql::ResultSet> res(select->executeQuery());
                if (!res->next())
                {
                    return std::nullopt;
                }
                return res->getUInt("Failed2faCount");
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::incrementFailed2fas");
                return std::nullopt;
            }
        }

        bool PersistentDatabase::lockTwoFactor(std::uint32_t accountId, std::uint64_t untilEpoch)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement("UPDATE Users SET TwoFactorLockedUntil = ?, Failed2faCount = 0 WHERE AccountID = ?"));
                stmt->setUInt64(1, untilEpoch);
                stmt->setUInt(2, accountId);
                return stmt->executeUpdate() > 0;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::lockTwoFactor");
                return false;
            }
        }

        void PersistentDatabase::resetFailedLogins(std::uint32_t accountId)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement("UPDATE Users SET FailedPasswordCount = 0, Failed2faCount = 0 WHERE AccountID = ?"));
                stmt->setUInt(1, accountId);
                stmt->executeUpdate();
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::resetFailedLogins");
            }
        }
    }
}
