#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Structures/AccountInfo/MainAccountInfo.h"
#include "Structures/Item/MainItem.h"
#include "Structures/Item/MainBoughtItem.h"
#include "Structures/Item/MainEquippedItem.h"
#include "Structures/Events/MainEventsList.h"
#include "Structures/PlayerLists/Friend.h"
#include "Structures/PlayerLists/BlockedPlayer.h"
#include "Structures/Mailbox/Mailbox.h"
#include "Structures/AccountInfo/MuteInfo.h"
#include "Persistence/TransactionGuard.h"
#include "Persistence/MainDatabaseManager.h"
#include "MainEnums.h"
#include "Utils/Constants.h"
#include <mariadb/conncpp.hpp>
#include <mariadb/conncpp/Driver.hpp>
#include <mariadb/conncpp/Connection.hpp>
#include "Utils/Logger.h"

namespace Main
{
    namespace Persistence
    {
        // The WHERE clause makes this atomic: of two uses of the same code, only one can move the step forward
        bool PersistentDatabase::consumeTotpStep(std::uint32_t accountID, std::uint64_t step)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement(
                    "UPDATE Users SET TotpLastStep = ? WHERE AccountID = ? AND TotpLastStep < ?"));
                stmt->setUInt64(1, step);
                stmt->setUInt(2, accountID);
                stmt->setUInt64(3, step);
                return stmt->executeUpdate() > 0;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::consumeTotpStep");
                return false;
            }
        }

        void PersistentDatabase::insertRecoveryCodes(std::uint32_t accountID, const std::vector<std::string>& hashes)
        {
            std::unique_ptr<sql::PreparedStatement> del(m_transactionalCon->prepareStatement("DELETE FROM TwoFactorRecoveryCodes WHERE AccountID = ?"));
            del->setUInt(1, accountID);
            del->executeUpdate();

            std::unique_ptr<sql::PreparedStatement> ins(m_transactionalCon->prepareStatement("INSERT INTO TwoFactorRecoveryCodes (AccountID, CodeHash) VALUES (?, ?)"));
            for (const auto& hash : hashes)
            {
                ins->setUInt(1, accountID);
                ins->setString(2, hash);
                ins->executeUpdate();
            }
        }

        bool PersistentDatabase::enableTwoFactor(std::uint32_t accountID, const std::string& encryptedSecret, std::uint64_t confirmedStep, const std::vector<std::string>& recoveryHashes)
        {
            try
            {
                TransactionGuard tg(m_transactionalCon.get());
                std::unique_ptr<sql::PreparedStatement> stmt(m_transactionalCon->prepareStatement(
                    "UPDATE Users SET Secret = ?, TwoFactorEnabled = 1, TotpLastStep = ?, Failed2faCount = 0, TwoFactorLockedUntil = 0 WHERE AccountID = ?"));
                stmt->setString(1, encryptedSecret);
                stmt->setUInt64(2, confirmedStep);
                stmt->setUInt(3, accountID);
                if (stmt->executeUpdate() != 1) return false;

                insertRecoveryCodes(accountID, recoveryHashes);
                tg.commit();
                return true;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::enableTwoFactor");
                return false;
            }
        }

        bool PersistentDatabase::disableTwoFactor(std::uint32_t accountID)
        {
            try
            {
                TransactionGuard tg(m_transactionalCon.get());
                std::unique_ptr<sql::PreparedStatement> stmt(m_transactionalCon->prepareStatement(
                    "UPDATE Users SET Secret = '', TwoFactorEnabled = 0, TotpLastStep = 0, Failed2faCount = 0, TwoFactorLockedUntil = 0 WHERE AccountID = ?"));
                stmt->setUInt(1, accountID);
                if (stmt->executeUpdate() != 1) return false;

                insertRecoveryCodes(accountID, {});
                tg.commit();
                return true;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::disableTwoFactor");
                return false;
            }
        }

        bool PersistentDatabase::replaceRecoveryCodes(std::uint32_t accountID, const std::vector<std::string>& recoveryHashes)
        {
            try
            {
                TransactionGuard tg(m_transactionalCon.get());
                insertRecoveryCodes(accountID, recoveryHashes);
                tg.commit();
                return true;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::replaceRecoveryCodes");
                return false;
            }
        }

        std::vector<std::pair<std::uint32_t, std::string>> PersistentDatabase::getUnusedRecoveryCodes(std::uint32_t accountID)
        {
            std::vector<std::pair<std::uint32_t, std::string>> rows;
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement(
                    "SELECT ID, CodeHash FROM TwoFactorRecoveryCodes WHERE AccountID = ? AND UsedAt IS NULL"));
                stmt->setUInt(1, accountID);
                std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
                while (res->next())
                {
                    rows.emplace_back(res->getUInt("ID"), res->getString("CodeHash").c_str());
                }
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::getUnusedRecoveryCodes");
            }
            return rows;
        }

        bool PersistentDatabase::consumeRecoveryCode(std::uint32_t codeId)
        {
            try
            {
                std::unique_ptr<sql::PreparedStatement> stmt(m_con->prepareStatement(
                    "UPDATE TwoFactorRecoveryCodes SET UsedAt = UTC_TIMESTAMP() WHERE ID = ? AND UsedAt IS NULL"));
                stmt->setUInt(1, codeId);
                return stmt->executeUpdate() > 0;
            }
            catch (const sql::SQLException& e)
            {
                ::Utils::Logger::log("MariaDB exception: " + std::string(e.what()), Utils::LogType::Error, "PersistentDatabase::consumeRecoveryCode");
                return false;
            }
        }
    }
}
