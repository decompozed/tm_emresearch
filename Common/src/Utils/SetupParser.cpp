/*
 * ToyBattlesHQ Emulator
 * Copyright (C) 2026 DownWithTheFallen
 *
 * This software is licensed under the ToyBattlesHQ Custom License (TBCL v1.0).
 * See LICENSE file in the project root for full terms.
 *
 * https://github.com/DownWithTheFallen/ToyBattlesHQ
 */


#include <mutex>
#include <sstream>
#include <algorithm>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include "../../include/Enums/MiscellaneousEnums.h"
#include "IniParser/inicpp.h"
#include "../../include/Utils/Utils.h"
#include "../../include/Utils/Logger.h"
#include "../../include/Utils/SetupParser.h"
#include "../../include/Utils/SecretSource.h"
#include <regex>
#include <charconv>



namespace Common
{
    namespace Utils
    {
        // A secret comes from the environment variable NAME or from the file named by NAME_FILE (see SecretSource.h).
        // A check function reports a problem and returns false; the getters run after the checks, so a wrong file stops the server here.
        static bool secretUsable(const std::string& name, const std::string& caller, std::string& value)
        {
            const SecretSource::Result result = SecretSource::read(name);
            if (result.origin == SecretSource::Origin::Error)
            {
                ::Utils::Logger::log(result.error, ::Utils::LogType::Error, caller);
                return false;
            }
            if (!result.ok())
            {
                ::Utils::Logger::log("Environment variable not set: " + name + " (or " + name + "_FILE)", ::Utils::LogType::Error, caller);
                return false;
            }
            value = result.value;
            return true;
        }

        static std::optional<std::string> readSecretOrStop(const std::string& name, const std::string& caller)
        {
            const SecretSource::Result result = SecretSource::read(name);
            if (result.origin == SecretSource::Origin::Error)
            {
                ::Utils::Logger::log(result.error, ::Utils::LogType::Error, caller);
                std::terminate();
            }
            if (!result.ok())
            {
                return std::nullopt;
            }
            return result.value;
        }

        SetupParser::SetupParser()
        {
            m_iniFile.load("../Setup/config.ini");
            if (!sanityCheck())
            {
                handleError("Sanity check failed");
            }

            check_assign(Common::Utils::getLocalIp(), m_localIp, "Could not retrieve local IP!");
            check_assign(getSelfMainServerInfoImpl(), m_selfMainInfo, "Could not retrieve self main info!");
            check_assign(getSelfCastServerInfoImpl(), m_selfCastInfo, "Could not retrieve self cast info!");
            check_assign(getMainServersInfoImpl(), m_mainInfos, "Could not retrieve main servers information!");
            check_assign(getCastServersInfoImpl(), m_castInfos, "Could not retrieve cast servers information!");
            m_authSetup = getAuthSetupImpl();
            m_dbSetup = getDatabaseSetupImpl();
            m_websiteSetup = getWebsiteSetupImpl();
            m_clientSetup = getClientSetupImpl();
            m_generalSetup = getGeneralSetupImpl();
            m_haveApiSetup = getHaveApiSetupImpl();
            m_ipcSetup = getIpcSetupImpl();
        }

        void SetupParser::handleError(const std::string& message)
        {
            ::Utils::Logger::log(message, ::Utils::LogType::Error, "SetupParser::SetupParser");
            std::cin.get(); 
            std::terminate(); 
        }

        bool SetupParser::checkMainCastSession()
        {
            const std::optional<std::string> localIp = Common::Utils::getLocalIp(); 
            if (!localIp)
            {
                ::Utils::Logger::log("Common::Utils::getLocalIp was std::nullopt!", ::Utils::LogType::Error, "SetupParser::checkMainCastSession");
                return false;
            }

            bool hasValidMainCastPair = false;
            for (std::uint32_t i = 1; i <= 9; ++i)
            {
                const std::string mainServerSection = "MainServer_" + std::to_string(i);
                const std::string castServerSection = "CastServer_" + std::to_string(i);

                if (m_iniFile.contains(mainServerSection) && m_iniFile.contains(castServerSection))
                {
                    ini::IniSection& mainServer = m_iniFile[mainServerSection];
                    std::string mainIp = mainServer["Ip"].as<std::string>();
                    std::uint32_t mainPort = mainServer["Port"].as<std::uint32_t>();
                    std::uint32_t mainIpcPort = mainServer["IpcPort"].as<std::uint32_t>();
                    std::string mainLocalIp = mainServer["LocalIp"].as<std::string>(); 
                    bool isPublic = mainServer["IsPublic"].as<bool>();

                    if (mainIp.empty() || mainPort == 0 || mainIpcPort == 0)
                    {
                        ::Utils::Logger::log(mainServerSection + " has wrong data in config.ini", ::Utils::LogType::Error, "SetupParser::checkMainCastSession");
                        return false;
                    }

                    ini::IniSection& castServer = m_iniFile[castServerSection];
                    std::string castIp = castServer["Ip"].as<std::string>();
                    std::uint32_t castPort = castServer["Port"].as<std::uint32_t>();
                    std::uint32_t castIpcPort = castServer["IpcPort"].as<std::uint32_t>();
                    std::string castLocalIp = castServer["LocalIp"].as<std::string>();

                    if (castIp.empty() || castPort == 0 || castIpcPort == 0)
                    {
                        ::Utils::Logger::log(castServerSection + " has wrong data in config.ini", ::Utils::LogType::Error, "SetupParser::checkMainCastSession");
                        return false;
                    }
                    if (!castServer.contains("IPC_EnableDeadBroadcast"))
                    {
                        ::Utils::Logger::log(castServerSection + " is missing 'IPC_EnableDeadBroadcast' key", ::Utils::LogType::Error, "SetupParser::checkMainCastSession");
                        return false;
                    }
                    try
                    {
                        bool ipcDeadBroadcast = castServer["IPC_EnableDeadBroadcast"].as<bool>();
                        (void)ipcDeadBroadcast; // unused variable warning
                    }
                    catch (const std::exception& e)
                    {
                        ::Utils::Logger::log(castServerSection + " has invalid 'IPC_EnableDeadBroadcast' value (must be true/false)", ::Utils::LogType::Error, "SetupParser::checkMainCastSession");
                        return false;
                    }

                    bool isMainLocalIpMatching = (mainLocalIp == *localIp);
                    bool isCastLocalIpMatching = (castLocalIp == *localIp);

                    if ((isMainLocalIpMatching && !isCastLocalIpMatching) || (!isMainLocalIpMatching && isCastLocalIpMatching))
                    {
                        ::Utils::Logger::log("MainServer and CastServer LocalIps don't match the current local IP", ::Utils::LogType::Error, 
                            "SetupParser::checkMainCastSession");
                        return false;
                    }

                    hasValidMainCastPair = true;
                }
                else if ((m_iniFile.contains(mainServerSection) && !m_iniFile.contains(castServerSection)) ||
                    (!m_iniFile.contains(mainServerSection) && m_iniFile.contains(castServerSection)))
                {
                    ::Utils::Logger::log("No corresponding MainServer/CastServer pair inside config.ini", ::Utils::LogType::Error, "SetupParser::checkMainCastSession");
                    return false;
                }
            }

            return hasValidMainCastPair;
        }

        bool SetupParser::checkAuthSection()
        {
            if (!m_iniFile.contains("AuthServer"))
            {
                ::Utils::Logger::log("Missing 'AuthServer' section in config.ini", ::Utils::LogType::Error, "SetupParser::checkAuthSection");
                return false;
            }

            ini::IniSection& authServer = m_iniFile["AuthServer"];

            if (!authServer.contains("Ip") || authServer["Ip"].as<std::string>().empty())
            {
                ::Utils::Logger::log("Missing/empty Ip in 'AuthServer' section", ::Utils::LogType::Error, "SetupParser::checkAuthSection");
                return false;
            }

            if (!authServer.contains("Port") || authServer["Port"].as<std::uint32_t>() == 0)
            {
                ::Utils::Logger::log("Missing or invalid Port in 'AuthServer' section", ::Utils::LogType::Error, "SetupParser::checkAuthSection");
                return false;
            }

            if (!authServer.contains("EnhancedSecurity"))
            {
                ::Utils::Logger::log("Missing EnhancedSecurity parameter in 'AuthServer' section", ::Utils::LogType::Error, "SetupParser::checkAuthSection");
                return false;
            }

            if (authServer["EnhancedSecurity"].as<bool>())
            {
                if (!authServer.contains("GradedPort") || authServer["GradedPort"].as<std::uint32_t>() == 0)
                {
                    ::Utils::Logger::log("Missing or invalid GradedPort in 'AuthServer' section while EnhancedSecurity is true", ::Utils::LogType::Error, "SetupParser::checkAuthSection");
                    return false;
                }

                if (!authServer.contains("VpnIp") || authServer["VpnIp"].as<std::string>().empty())
                {
                    ::Utils::Logger::log("Missing or empty VpnIp in 'AuthServer' section while EnhancedSecurity is true", ::Utils::LogType::Error, "SetupParser::checkAuthSection");
                    return false;
                }
            }

            return true;
        }

        bool SetupParser::checkDatabaseConfig()
        {
            const std::string databaseSection = "Database";

            if (!m_iniFile.contains(databaseSection))
            {
                ::Utils::Logger::log("[Database] section missing in config.ini", ::Utils::LogType::Error, "SetupParser::checkDatabaseConfig");
                return false;
            }

            ini::IniSection& database = m_iniFile[databaseSection];
            const std::string passwordEnv = database["PasswordEnvironmentName"].as<std::string>();

            if (database["LocalIp"].as<std::string>().empty() || database["Ip"].as<std::string>().empty()
                || database["Port"].as<std::uint32_t>() == 0 || database["DatabaseName"].as<std::string>().empty()
                || database["Username"].as<std::string>().empty() || passwordEnv.empty())
            {
                ::Utils::Logger::log("Invalid database configuration in config.ini", ::Utils::LogType::Error, "SetupParser::checkDatabaseConfig");
                return false;
            }

            std::string password;
            if (!secretUsable(passwordEnv, "SetupParser::checkDatabaseConfig", password))
            {
                return false;
            }
            return true;
        }

        bool SetupParser::checkWebsiteConfig()
        {
            if (!m_iniFile.contains("Website"))
            {
                ::Utils::Logger::log("[Website] section missing in config.ini",::Utils::LogType::Error,"SetupParser::checkWebsiteConfig");
                return false;
            }

            ini::IniSection& website = m_iniFile["Website"];

            if (website["Ip"].as<std::string>().empty() || website["Port"].as<std::uint32_t>() == 0)
            {
                ::Utils::Logger::log("Invalid website IP or Port configuration in config.ini",::Utils::LogType::Error,"SetupParser::checkWebsiteConfig");
                return false;
            }

            if (website["JwtTokenEnvironmentName"].as<std::string>().empty())
            {
                ::Utils::Logger::log("JwtTokenEnvironmentName missing or empty in config.ini",::Utils::LogType::Error,"SetupParser::checkWebsiteConfig");
                return false;
            }

            std::string jwtKey;
            if (!secretUsable(website["JwtTokenEnvironmentName"].as<std::string>(), "SetupParser::checkWebsiteConfig", jwtKey))
            {
                return false;
            }
            if (jwtKey.empty())
            {
                ::Utils::Logger::log("The JWT secret is empty", ::Utils::LogType::Error, "SetupParser::checkWebsiteConfig");
                return false;
            }

            // Optional: the key that was valid before a rotation, accepted for tokens that are still in circulation
            if (website.contains("JwtTokenPreviousEnvironmentName") && !website["JwtTokenPreviousEnvironmentName"].as<std::string>().empty())
            {
                const SecretSource::Result previous = SecretSource::read(website["JwtTokenPreviousEnvironmentName"].as<std::string>());
                if (previous.origin == SecretSource::Origin::Error)
                {
                    ::Utils::Logger::log(previous.error, ::Utils::LogType::Error, "SetupParser::checkWebsiteConfig");
                    return false;
                }
            }

            if (!website.contains("AllowedOrigins") || website["AllowedOrigins"].as<std::string>().empty())
            {
                ::Utils::Logger::log("AllowedOrigins missing or empty in config.ini",::Utils::LogType::Error,"SetupParser::checkWebsiteConfig");
                return false;
            }

            // Optional: the name of the variable (or NAME_FILE) that holds the Cloudflare Turnstile secret of the account API
            if (website.contains("TurnstileSecret") && !website["TurnstileSecret"].as<std::string>().empty())
            {
                const SecretSource::Result turnstile = SecretSource::read(website["TurnstileSecret"].as<std::string>());
                if (turnstile.origin == SecretSource::Origin::Error)
                {
                    ::Utils::Logger::log(turnstile.error, ::Utils::LogType::Error, "SetupParser::checkWebsiteConfig");
                    return false;
                }
            }

            return true;
        }

        bool SetupParser::checkClientConfig()
        {
            if (!m_iniFile.contains("Client"))
            {
                ::Utils::Logger::log("[Client] section missing in config.ini", ::Utils::LogType::Error, "SetupParser::checkClientConfig");
                return false;
            }

            ini::IniSection& client = m_iniFile["Client"];

            const std::string& versionStr = client["ClientVersion"].as<std::string>();

            const std::regex versionRegex(R"(^\d\.\d\.\d$)");
            if (!std::regex_match(versionStr, versionRegex))
            {
                ::Utils::Logger::log("Invalid ClientVersion format - expected X.X.X with one digit per part (e.g., 1.2.3)",
                    ::Utils::LogType::Error, "SetupParser::checkClientConfig");
                return false;
            }

            return true;
        }


        bool SetupParser::sanityCheck()
        {
            return checkAuthSection() && checkGeneralConfig() && checkMainCastSession() && checkDatabaseConfig() && checkWebsiteConfig() && checkClientConfig()
                && checkIpcConfig();
        }

        std::string SetupParser::ipcSecretEnvironmentName()
        {
            if (!m_iniFile.contains("Ipc") || !m_iniFile["Ipc"].contains("SecretEnvironmentName"))
            {
                return {};
            }
            return trim(m_iniFile["Ipc"]["SecretEnvironmentName"].as<std::string>());
        }

        bool SetupParser::checkIpcConfig()
        {
            const std::string environmentName = ipcSecretEnvironmentName();
            if (environmentName.empty())
            {
                return true;
            }
            std::string value;
            if (!secretUsable(environmentName, "SetupParser::checkIpcConfig", value))
            {
                return false;
            }
            const std::string hex = trim(value);
            if (hex.size() != 64 || !std::ranges::all_of(hex, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
            {
                ::Utils::Logger::log("The environment variable " + environmentName + " must be 64 hex digits (32 bytes, e.g. openssl rand -hex 32)",
                    ::Utils::LogType::Error, "SetupParser::checkIpcConfig");
                return false;
            }
            return true;
        }

        IpcSetup SetupParser::getIpcSetupImpl()
        {
            IpcSetup setup;
            const std::string environmentName = ipcSecretEnvironmentName();
            const std::optional<std::string> value = environmentName.empty() ? std::nullopt : readSecretOrStop(environmentName, "SetupParser::getIpcSetupImpl");
            if (!value)
            {
                return setup;
            }
            const std::string hex = trim(*value);
            setup.secret.resize(hex.size() / 2);
            CryptoPP::StringSource source(hex, true, new CryptoPP::HexDecoder(new CryptoPP::ArraySink(setup.secret.data(), setup.secret.size())));
            return setup;
        }

        HaveApiSetup SetupParser::getHaveApiSetupImpl()
        {
            HaveApiSetup setup;
            if (!m_iniFile.contains("HaveApi"))
            {
                return setup;
            }
            ini::IniSection& section = m_iniFile["HaveApi"];
            if (section.contains("RoomOptionCheckBox"))
            {
                setup.roomOptionCheckBox = section["RoomOptionCheckBox"].as<std::uint32_t>() != 0;
            }
            if (section.contains("RequireDll"))
            {
                setup.requireDll = section["RequireDll"].as<std::uint32_t>() != 0;
            }
            if (section.contains("DllFiles"))
            {
                setup.dllFiles = section["DllFiles"].as<std::string>();
            }
            if (section.contains("RoundsPerMatch"))
            {
                setup.roundsPerMatch = (std::max)(1u, section["RoundsPerMatch"].as<std::uint32_t>());
            }
            if (section.contains("KillPoints"))
            {
                setup.killPoints = section["KillPoints"].as<std::int32_t>();
            }
            if (section.contains("LastStandingPoints"))
            {
                setup.lastStandingPoints = section["LastStandingPoints"].as<std::int32_t>();
            }
            if (section.contains("MarkedTargetPoints"))
            {
                setup.markedTargetPoints = section["MarkedTargetPoints"].as<std::int32_t>();
            }
            if (section.contains("LavaSpeedScale"))
            {
                setup.lavaSpeedScale = section["LavaSpeedScale"].as<float>();
            }
            if (section.contains("LavaWallCloseSeconds"))
            {
                setup.lavaWallCloseSeconds = (std::max)(10.0f, section["LavaWallCloseSeconds"].as<float>());
            }
            if (section.contains("LavaRoundSeconds"))
            {
                setup.lavaRoundSeconds = (std::max)(30u, section["LavaRoundSeconds"].as<std::uint32_t>());
            }
            if (section.contains("BattleRoyaleEnabled"))
            {
                setup.battleRoyaleEnabled = section["BattleRoyaleEnabled"].as<std::uint32_t>() != 0;
            }
            if (section.contains("MapPartyModes"))
            {
                std::stringstream entries{ section["MapPartyModes"].as<std::string>() };
                for (std::string entry; std::getline(entries, entry, ',');)
                {
                    const std::size_t colon = entry.find(':');
                    if (colon == std::string::npos)
                    {
                        ::Utils::Logger::log("[HaveApi] MapPartyModes: ignored entry '" + entry + "' (expected map:mode)", ::Utils::LogType::Warning);
                        continue;
                    }
                    const auto mapId = static_cast<std::uint16_t>(std::stoul(trim(entry.substr(0, colon))));
                    const auto modeId = static_cast<std::uint16_t>(std::stoul(trim(entry.substr(colon + 1))));
                    setup.mapPartyModes.emplace_back(mapId, modeId);
                }
            }
            return setup;
        }

        // The value of the setting names an environment variable holding 32 bytes in hex, like the other secrets
        static std::optional<std::string> readPepperKey(ini::IniSection& section, const std::string& setting)
        {
            if (!section.contains(setting))
            {
                return std::nullopt;
            }

            const std::optional<std::string> keyHex = readSecretOrStop(section[setting].as<std::string>(), "SetupParser::readPepperKey");
            if (!keyHex)
            {
                return std::nullopt;
            }

            std::string decoded;
            if (keyHex->size() == PasswordHash::pepperBytes * 2)
            {
                CryptoPP::StringSource ss(*keyHex, true, new CryptoPP::HexDecoder(new CryptoPP::StringSink(decoded)));
            }

            if (decoded.size() != PasswordHash::pepperBytes)
            {
                ::Utils::Logger::log(setting + " must be 32 bytes in hex", ::Utils::LogType::Error, "SetupParser::readPepperKey");
                return std::nullopt;
            }
            return decoded;
        }

        static PasswordHash::Peppers readPasswordPeppers(ini::IniFile& file)
        {
            PasswordHash::Peppers peppers;
            if (file.contains("General"))
            {
                ini::IniSection& section = file["General"];
                const std::uint32_t version = section.contains("PasswordPepperVersion") ? section["PasswordPepperVersion"].as<std::uint32_t>() : 1;
                if (const auto key = readPepperKey(section, "PasswordPepper"))
                {
                    peppers.current = PasswordHash::Pepper{ version, *key };
                }
                if (const auto key = readPepperKey(section, "PasswordPepperPrevious"); key && version > 0)
                {
                    peppers.previous = PasswordHash::Pepper{ version - 1, *key };
                }
            }

            if (!peppers.current)
            {
                ::Utils::Logger::log("No password pepper configured: passwords stay on bcrypt", ::Utils::LogType::Warning, "SetupParser::readPasswordPeppers");
            }
            return peppers;
        }

        GeneralSetup SetupParser::getGeneralSetupImpl()
        {
            GeneralSetup generalSetup;
            generalSetup.passwordPeppers = readPasswordPeppers(m_iniFile);

            if (!m_authSetup.enhancedSecurity)
            {
                // Player 2FA (/twofa) only needs this key, not the VPN and SMTP set-up of EnhancedSecurity.
                // Without a valid key it stays empty and the feature is reported as unavailable.
                if (m_iniFile.contains("General") && m_iniFile["General"].contains("2faSecret"))
                {
                    const std::optional<std::string> keyHex = readSecretOrStop(m_iniFile["General"]["2faSecret"].as<std::string>(), "SetupParser::getGeneralSetupImpl");
                    if (keyHex && keyHex->size() == 64)
                    {
                        std::string decoded;
                        CryptoPP::StringSource ss(*keyHex, true, new CryptoPP::HexDecoder(new CryptoPP::StringSink(decoded)));
                        if (decoded.size() == 32)
                        {
                            generalSetup.twoFaSecret.Assign(reinterpret_cast<const CryptoPP::byte*>(decoded.data()), decoded.size());
                        }
                    }
                }
                // The account API of the website stores the email encrypted with this key, also without EnhancedSecurity
                if (m_iniFile.contains("General") && m_iniFile["General"].contains("EmailSecret"))
                {
                    const std::optional<std::string> keyHex = readSecretOrStop(m_iniFile["General"]["EmailSecret"].as<std::string>(), "SetupParser::getGeneralSetupImpl");
                    if (keyHex && keyHex->size() == 64)
                    {
                        std::string decoded;
                        CryptoPP::StringSource ss(*keyHex, true, new CryptoPP::HexDecoder(new CryptoPP::StringSink(decoded)));
                        if (decoded.size() == 32)
                        {
                            generalSetup.emailSecret.Assign(reinterpret_cast<const CryptoPP::byte*>(decoded.data()), decoded.size());
                        }
                    }
                }
                return generalSetup;
            }

            ini::IniSection& section = m_iniFile["General"];

            const std::string emailSecretEnv = section["EmailSecret"].as<std::string>();
            const std::string twoFaSecretEnv = section["2faSecret"].as<std::string>();
            const std::string emailTokenEnv = section["EmailToken"].as<std::string>();

            const std::string emailSecretVal = readSecretOrStop(emailSecretEnv, "SetupParser::getGeneralSetupImpl").value_or("");
            const std::string twoFaSecretVal = readSecretOrStop(twoFaSecretEnv, "SetupParser::getGeneralSetupImpl").value_or("");
            const std::string emailTokenVal = readSecretOrStop(emailTokenEnv, "SetupParser::getGeneralSetupImpl").value_or("");

            generalSetup.emailSecret.resize(32);
            generalSetup.twoFaSecret.resize(32);

            {
                CryptoPP::StringSource ss(emailSecretVal, true, new CryptoPP::HexDecoder(new CryptoPP::ArraySink(generalSetup.emailSecret.data(), generalSetup.emailSecret.size())));
            }

            {
                CryptoPP::StringSource ss(twoFaSecretVal, true, new CryptoPP::HexDecoder(new CryptoPP::ArraySink(generalSetup.twoFaSecret.data(), generalSetup.twoFaSecret.size())));
            }

            generalSetup.emailToken = emailTokenVal;
            generalSetup.emailUsername = section["EmailUsername"].as<std::string>();
            generalSetup.smtpServer = section["SmtpServer"].as<std::string>();
            generalSetup.email = section["EmailSender"].as<std::string>();

            generalSetup.securityNotificationEmails.clear();
            std::string emailsStr = section["SecurityNotificationReceiver"].as<std::string>();

            if (!emailsStr.empty())
            {
                std::stringstream ss(emailsStr);
                std::string email;
                std::string emailList;

                while (std::getline(ss, email, ','))
                {
                    email = trim(email);

                    if (!email.empty())
                    {
                        generalSetup.securityNotificationEmails.push_back(email);

                        if (!emailList.empty()) emailList += ", ";
                        emailList += email;
                    }
                }

                ::Utils::Logger::log("Added security notification emails: " + emailList, ::Utils::LogType::Info, "SetupParser::getGeneralSetupImpl");
            }

            return generalSetup;
        }


        bool SetupParser::checkGeneralConfig()
        {
            if (m_iniFile.contains("AuthServer") && m_iniFile["AuthServer"].contains("EnhancedSecurity")
                && !m_iniFile["AuthServer"]["EnhancedSecurity"].as<bool>())
            {
                return true;
            }

            if (!m_iniFile.contains("General"))
            {
                ::Utils::Logger::log("[General] section missing in config.ini",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            ini::IniSection& general = m_iniFile["General"];

            std::string emailSecretEnv = general["EmailSecret"].as<std::string>();
            if (emailSecretEnv.empty())
            {
                ::Utils::Logger::log("EmailSecret missing or empty in [General]",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            std::string emailSecretVal;
            if (!secretUsable(emailSecretEnv, "SetupParser::checkGeneralConfig", emailSecretVal))
            {
                return false;
            }

            try
            {
                std::string decoded;
                CryptoPP::StringSource ss(emailSecretVal,true,new CryptoPP::HexDecoder(new CryptoPP::StringSink(decoded)));

                if (decoded.size() != 32)
                {
                    ::Utils::Logger::log("EmailSecret must decode to exactly 32 bytes (AES-256)",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                    return false;
                }
            }
            catch (...)
            {
                ::Utils::Logger::log("EmailSecret is not valid hex",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            std::string twoFaSecretEnv = general["2faSecret"].as<std::string>();
            if (twoFaSecretEnv.empty())
            {
                ::Utils::Logger::log("2faSecret missing or empty in [General]",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            std::string twoFaSecretVal;
            if (!secretUsable(twoFaSecretEnv, "SetupParser::checkGeneralConfig", twoFaSecretVal))
            {
                return false;
            }

            std::string emailUsername = general["EmailUsername"].as<std::string>();
            if (emailUsername.empty())
            {
                ::Utils::Logger::log("EmailUsername missing or empty in [General]", ::Utils::LogType::Error, "SetupParser::checkGeneralConfig");
                return false;
            }

            try
            {
                std::string decoded;
                CryptoPP::StringSource ss(twoFaSecretVal,true,new CryptoPP::HexDecoder(new CryptoPP::StringSink(decoded)));

                if (decoded.size() != 32)
                {
                    ::Utils::Logger::log("2faSecret must decode to exactly 32 bytes (AES-256)",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                    return false;
                }
            }
            catch (...)
            {
                ::Utils::Logger::log("2faSecret is not valid hex",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            if (general["SmtpServer"].as<std::string>().empty())
            {
                ::Utils::Logger::log("SmtpServer missing or empty in [General]",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            if (general["EmailSender"].as<std::string>().empty())
            {
                ::Utils::Logger::log("EmailSender missing or empty in [General]",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            if (general["EmailToken"].as<std::string>().empty())
            {
                ::Utils::Logger::log("EmailToken missing or empty in [General]",::Utils::LogType::Error,"SetupParser::checkGeneralConfig");
                return false;
            }

            if (general["SecurityNotificationReceiver"].as<std::string>().empty())
            {
                ::Utils::Logger::log("SecurityNotificationReceiver missing or empty in [General]", ::Utils::LogType::Error, "SetupParser::checkSecurityConfig");
                return false;
            }

            return true;
        }


        AuthSetup SetupParser::getAuthSetupImpl()
        {
            AuthSetup auth{};
            auth.ip = m_iniFile["AuthServer"]["Ip"].as<std::string>();
            auth.port = m_iniFile["AuthServer"]["Port"].as<std::uint32_t>();
            auth.enhancedSecurity = m_iniFile["AuthServer"]["EnhancedSecurity"].as<bool>();

            if (auth.enhancedSecurity)
            {
                auth.gradedPort = m_iniFile["AuthServer"]["GradedPort"].as<std::uint32_t>();
                auth.vpnIp = m_iniFile["AuthServer"]["VpnIp"].as<std::string>();
            }

            return auth;
        }

        ClientSetup SetupParser::getClientSetupImpl()
        {
            ClientSetup client;

            const std::string& versionStr = m_iniFile["Client"]["ClientVersion"].as<std::string>();
            const std::regex versionRegex(R"(^\d\.\d\.\d$)");

            if (!std::regex_match(versionStr, versionRegex))
            {
                ::Utils::Logger::log("Invalid ClientVersion format. Expected X.X.X with one digit per part (e.g., 1.2.3)",
                    ::Utils::LogType::Error, "SetupParser::getClientSetupImpl");
                return client; 
            }

            client.version1 = versionStr[0] - '0'; 
            client.version2 = versionStr[2] - '0'; 
            client.version3 = versionStr[4] - '0'; 

            return client;
        }


        std::optional<MainSetup> SetupParser::getSelfMainServerInfoImpl()
        {
            auto mainServersInfoOpt = getMainServersInfoImpl();
            if (!mainServersInfoOpt) return std::nullopt;

            for (const auto mainServerInfo : *mainServersInfoOpt)
            {
                if (mainServerInfo.localIp == m_localIp)
                {
                    return mainServerInfo;
                }
            }
            return std::nullopt;
        }

        std::optional<CastSetup> SetupParser::getSelfCastServerInfoImpl()
        {
            auto castServersInfoOpt = getCastServersInfoImpl();
            if (!castServersInfoOpt)
                return std::nullopt;

            std::uint32_t selfServerNumber = m_selfMainInfo.serverNumber;

            for (const auto& castServerInfo : *castServersInfoOpt)
            {
                if (castServerInfo.serverNumber == selfServerNumber)
                {
                    return castServerInfo;
                }
            }

            return std::nullopt;
        }


        std::optional<std::vector<MainSetup>> SetupParser::getMainServersInfoImpl()
        {
            std::vector<MainSetup> mainServers;
            for (auto& sectionPair : m_iniFile)
            {
                const std::string& sectionName = sectionPair.first;
                ini::IniSection& section = sectionPair.second;

                if (sectionName.find("MainServer_") == 0)
                {
                    MainSetup main;
                    main.ip = section["Ip"].as<std::string>();
                    main.port = section["Port"].as<std::uint32_t>();
                    main.ipcPort = section["IpcPort"].as<std::uint32_t>();
                    main.serverNumber = std::stoi(sectionName.substr(11));
                    main.localIp = section["LocalIp"].as<std::string>();
                    main.isPublic = section["IsPublic"].as<bool>();
                  
                    mainServers.push_back(main);
                }
            }
            if (mainServers.empty()) return std::nullopt;
            return mainServers;
        }

        std::optional<std::vector<CastSetup>> SetupParser::getCastServersInfoImpl()
        {
            std::vector<CastSetup> castServers;
            for (auto& sectionPair : m_iniFile)
            {
                const std::string& sectionName = sectionPair.first;
                ini::IniSection& section = sectionPair.second;

                if (sectionName.find("CastServer_") == 0)
                {
                    CastSetup cast;
                    cast.ip = section["Ip"].as<std::string>();
                    cast.port = section["Port"].as<std::uint32_t>();
                    cast.ipcPort = section["IpcPort"].as<std::uint32_t>();
                    cast.serverNumber = std::stoi(sectionName.substr(11));
                    cast.localIp = section["LocalIp"].as<std::string>();
                    cast.IPC_enableDeadBroadcast = section["IPC_EnableDeadBroadcast"].as<bool>();
                    castServers.push_back(cast);
                }
            }
            if (castServers.empty()) return std::nullopt;
            return castServers;
        }

        DatabaseSetup SetupParser::getDatabaseSetupImpl()
        {
            DatabaseSetup db;
            db.ip = m_iniFile["Database"]["Ip"].as<std::string>();
            db.port = m_iniFile["Database"]["Port"].as<std::uint32_t>();
            db.databaseName = m_iniFile["Database"]["DatabaseName"].as<std::string>();
            db.username = m_iniFile["Database"]["Username"].as<std::string>();

            std::string envVarName = m_iniFile["Database"]["PasswordEnvironmentName"].as<std::string>();
            db.password = readSecretOrStop(envVarName, "SetupParser::getDatabaseSetupImpl").value_or("");

            return db;
        }

        WebsiteSetup SetupParser::getWebsiteSetupImpl()
        {
            WebsiteSetup website;

            ini::IniSection& section = m_iniFile["Website"];

            website.ip = section["Ip"].as<std::string>();
            website.port = section["Port"].as<std::uint32_t>();

            std::string jwtEnvName = section["JwtTokenEnvironmentName"].as<std::string>();
            const std::optional<std::string> jwtEnvValue = readSecretOrStop(jwtEnvName, "SetupParser::getWebsiteSetupImpl");
            if (jwtEnvValue)
                website.jwtToken = *jwtEnvValue;
            else
            {
                ::Utils::Logger::log("Environment variable for JwtTokenEnvironmentName (" + jwtEnvName + ") not set",::Utils::LogType::Error,"SetupParser::getWebsiteSetupImpl");
            }

            if (section.contains("JwtTokenPreviousEnvironmentName") && !section["JwtTokenPreviousEnvironmentName"].as<std::string>().empty())
            {
                website.jwtTokenPrevious = readSecretOrStop(section["JwtTokenPreviousEnvironmentName"].as<std::string>(), "SetupParser::getWebsiteSetupImpl").value_or("");
            }

            website.allowedOrigins.clear();
            std::string originsStr = section["AllowedOrigins"].as<std::string>();
            std::stringstream ss(originsStr);
            std::string origin;

            while (std::getline(ss, origin, ','))
            {
                origin = trim(origin);
                if (!origin.empty())
                    website.allowedOrigins.push_back(origin);
            }

            website.trustedProxies.clear();
            if (section.contains("TrustedProxy"))
            {
                std::stringstream proxies(section["TrustedProxy"].as<std::string>());
                std::string proxy;
                while (std::getline(proxies, proxy, ','))
                {
                    proxy = trim(proxy);
                    if (!proxy.empty())
                        website.trustedProxies.push_back(proxy);
                }
            }

            if (section.contains("TurnstileSecret") && !section["TurnstileSecret"].as<std::string>().empty())
            {
                website.turnstileSecret = readSecretOrStop(section["TurnstileSecret"].as<std::string>(), "SetupParser::getWebsiteSetupImpl").value_or("");
            }
            if (website.turnstileSecret.empty())
            {
                ::Utils::Logger::log("[Website] TurnstileSecret is not configured: the captcha check of POST /account/register is SKIPPED (local tests only)",
                    ::Utils::LogType::Warning, "SetupParser::getWebsiteSetupImpl");
            }

            // Optional, 1 to 10000. A value that is not a plain number in that range keeps the default and is logged.
            const auto readLimit = [this, &section](const char* key, std::uint32_t& target)
            {
                if (!section.contains(key))
                    return;
                const std::string text = trim(section[key].as<std::string>());
                std::uint32_t value = 0;
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (error != std::errc{} || end != text.data() + text.size() || value < 1 || value > 10000)
                {
                    ::Utils::Logger::log(std::string("[Website] ") + key + " must be a number from 1 to 10000, keeping " + std::to_string(target),
                        ::Utils::LogType::Warning, "SetupParser::getWebsiteSetupImpl");
                    return;
                }
                target = value;
            };
            readLimit("RegisterLimitPerHour", website.registerLimitPerHour);
            readLimit("LoginLimitPer15Min", website.loginLimitPer15Min);
            readLimit("PasswordLimitPer15Min", website.passwordLimitPer15Min);

            return website;
        }
    };
}

