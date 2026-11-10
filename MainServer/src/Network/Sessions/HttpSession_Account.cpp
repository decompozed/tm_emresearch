// The player account API of the website: GET /account/status, POST /account/register, POST /account/login, GET /account/me
// and POST /account/password. The rules (names, passwords, rate limits, which address is the client) are in
// Utils/AccountApi.h, the token in Utils/WebToken.h.

#include "Network/Sessions/HttpSession.h"
#include "Network/Turnstile.h"
#include "Utils/AccountApi.h"
#include "Utils/WebToken.h"
#include "Utils/SecurityEvent.h"
#include "Utils/Totp.h"
#include "Utils/Utils.h"

#include <libbcrypt/include/bcrypt/BCrypt.hpp>

#include <chrono>
#include <ctime>
#include <mutex>

namespace Main
{
    namespace Network
    {
        namespace Api = Common::Utils::AccountApi;
        namespace SecEv = Common::Utils::SecurityEvent;
        namespace Totp = Common::Utils::Totp;
        using Main::Structures::WebAccount;
        using Main::Structures::WebAccountLookup;

        namespace
        {
            using namespace std::chrono_literals;

            // The limits come from [Website] (defaults 5 per hour, 10 and 5 per 15 minutes), read once on the first request
            const Common::Utils::WebsiteSetup& websiteSetup() { return Common::Utils::SetupParser::getInstance().getWebsiteSetup(); }
            Api::RateLimiter& registerLimiter() { static Api::RateLimiter limiter(websiteSetup().registerLimitPerHour, 1h); return limiter; }
            Api::RateLimiter& loginLimiter() { static Api::RateLimiter limiter(websiteSetup().loginLimitPer15Min, 15min); return limiter; }
            Api::RateLimiter& passwordLimiter() { static Api::RateLimiter limiter(websiteSetup().passwordLimitPer15Min, 15min); return limiter; }

            constexpr std::uint32_t firstStaffGrade = 3; // GRADE_MOD: the staff endpoints need this grade, the website never signs such an account in

            std::string errorBody(const char* code)
            {
                boost::json::object body;
                body["error"] = code;
                return boost::json::serialize(body);
            }

            // A hash to spend the same time on when the account does not exist, so the answer time does not tell which names exist
            const std::string& dummyPasswordHash()
            {
                static const std::string hash = Common::Utils::hashPassword("toymayhem-dummy-password-for-timing");
                return hash;
            }

            bool tcpReachable(const std::string& ip, std::uint32_t port)
            {
                try
                {
                    boost::asio::io_context io;
                    boost::beast::tcp_stream stream(io);
                    boost::system::error_code result = boost::asio::error::would_block;
                    const auto endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(ip), static_cast<unsigned short>(port));
                    stream.expires_after(std::chrono::milliseconds(250));
                    stream.async_connect(endpoint, [&](const boost::system::error_code& ec) { result = ec; });
                    io.run();
                    return !result;
                }
                catch (const std::exception&)
                {
                    return false;
                }
            }

            struct ServerStatus
            {
                bool auth{};
                bool cast{};
            };

            // Looked up at most once every 5 seconds, so the public status page cannot be used to make the server open connections
            ServerStatus serverStatus()
            {
                static std::mutex mutex;
                static ServerStatus cached;
                static std::chrono::steady_clock::time_point checkedAt{};
                std::lock_guard<std::mutex> lock(mutex);
                const auto now = std::chrono::steady_clock::now();
                if (checkedAt == std::chrono::steady_clock::time_point{} || now - checkedAt > 5s)
                {
                    const auto& setup = Common::Utils::SetupParser::getInstance();
                    cached.auth = tcpReachable(setup.getAuthSetup().ip, setup.getAuthSetup().port);
                    cached.cast = tcpReachable(setup.getSelfCastServerInfo().ip, setup.getSelfCastServerInfo().port);
                    checkedAt = now;
                }
                return cached;
            }

            const boost::json::value* field(const boost::json::object& obj, const char* name)
            {
                return obj.if_contains(name);
            }

            // A string field; false when missing or not a string
            bool getString(const boost::json::object& obj, const char* name, std::string& out)
            {
                const boost::json::value* value = field(obj, name);
                if (!value || !value->is_string())
                {
                    return false;
                }
                out = std::string(value->as_string());
                return true;
            }
        }

        std::string HttpSession::clientIp() const
        {
            return Common::Utils::AccountApi::clientIp(m_peerIp, m_request["CF-Connecting-IP"], Common::Utils::SetupParser::getInstance().getWebsiteSetup().trustedProxies);
        }

        void HttpSession::apiError(std::string& responseBody, http::status& statusCode, http::status status, const char* code)
        {
            statusCode = status;
            responseBody = errorBody(code);
        }

        void HttpSession::apiEvent(const char* event, const char* severity, std::uint32_t accountId, const char* result, const std::string& reason)
        {
            SecEv::emit(event, severity, accountId, clientIp(), result, reason);
        }

        bool HttpSession::apiRateLimited(Api::RateLimiter& limiter, const char* endpoint, std::string& responseBody, http::status& statusCode)
        {
            if (limiter.allow(clientIp()))
            {
                return false;
            }
            apiEvent("authn_fail", "MEDIUM", 0, "denied", std::string{ "rate_limited " } + endpoint);
            apiError(responseBody, statusCode, http::status::too_many_requests, "rate_limited");
            return true;
        }

        bool HttpSession::apiParseObject(boost::json::object& out)
        {
            if (m_request.body().size() > Api::maxBodyBytes)
            {
                return false;
            }
            boost::system::error_code ec;
            boost::json::parse_options options;
            options.max_depth = 8;
            boost::json::value parsed = boost::json::parse(m_request.body(), ec, {}, options);
            if (ec || !parsed.is_object())
            {
                return false;
            }
            out = std::move(parsed.as_object());
            return true;
        }

        std::optional<Common::Utils::WebToken::Claims> HttpSession::apiBearer()
        {
            const std::string header = std::string(m_request[http::field::authorization]);
            constexpr std::string_view scheme = "Bearer ";
            if (header.size() <= scheme.size() || header.size() > scheme.size() + 2048 || header.compare(0, scheme.size(), scheme) != 0)
            {
                return std::nullopt;
            }
            const auto& website = Common::Utils::SetupParser::getInstance().getWebsiteSetup();
            return Common::Utils::WebToken::verify(header.substr(scheme.size()), website.jwtToken, website.jwtTokenPrevious);
        }

        // Same rules as the game login for an account with 2FA on (AuthService::authorizeWithSecondFactor): a code is only looked
        // at after the password was right, a used code is refused, five wrong codes lock the second factor for 15 minutes.
        HttpSession::SecondFactor HttpSession::apiCheckSecondFactor(const WebAccount& account, const std::string& rawCode)
        {
            const std::uint64_t now = static_cast<std::uint64_t>(std::time(nullptr));
            if (account.twoFactorLockedUntil > now)
            {
                return SecondFactor::Locked;
            }
            if (rawCode.empty())
            {
                return SecondFactor::Missing;
            }

            const std::string code = Totp::toUpper(rawCode);
            SecondFactor outcome = SecondFactor::Invalid;
            if (Totp::isRecoveryCode(code))
            {
                const auto rows = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::getUnusedRecoveryCodes, account.accountId);
                for (const auto& [id, hash] : rows)
                {
                    if (!BCrypt::validatePassword(code, hash))
                    {
                        continue;
                    }
                    const bool consumed = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::consumeRecoveryCode, id);
                    outcome = consumed ? SecondFactor::Valid : SecondFactor::Replayed;
                    break;
                }
            }
            else if (Totp::isTotpCode(code))
            {
                const auto secret = Common::Utils::decrypt2FASecret(account.encryptedSecret, Common::Utils::SetupParser::getInstance().getGeneralSetup().twoFaSecret);
                if (secret)
                {
                    const Totp::MatchResult match = Totp::matchCode(*secret, code, std::time(nullptr), account.totpLastStep);
                    if (match.status == Totp::MatchStatus::Valid)
                    {
                        const bool consumed = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::consumeTotpStep,
                            account.accountId, match.step);
                        outcome = consumed ? SecondFactor::Valid : SecondFactor::Replayed;
                    }
                    else if (match.status == Totp::MatchStatus::Replayed)
                    {
                        outcome = SecondFactor::Replayed;
                    }
                }
            }

            if (outcome == SecondFactor::Invalid)
            {
                const auto wrong = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::incrementFailed2fas, account.accountId);
                if (wrong && *wrong >= Totp::maxWrongCodesPlayer)
                {
                    m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::lockTwoFactor, account.accountId, now + Totp::playerLockSeconds);
                    apiEvent("authn_lockout", "HIGH", account.accountId, "locked", "too_many_wrong_2fa_codes");
                }
            }
            return outcome;
        }

        /*
           GET /account/status (public)
           200 {"online": n, "servers": {"auth": bool, "main": true, "cast": bool}}
        */
        void HttpSession::handleAccountStatus(std::string& responseBody, http::status& statusCode)
        {
            const ServerStatus status = serverStatus();
            boost::json::object servers;
            servers["auth"] = status.auth;
            servers["main"] = true;
            servers["cast"] = status.cast;
            boost::json::object body;
            body["online"] = m_sessionsManager.getAllSessions().size();
            body["servers"] = std::move(servers);
            statusCode = http::status::ok;
            responseBody = boost::json::serialize(body);
        }

        /*
           POST /account/register (public) {"username","nickname","password","email","turnstile"}
           201 {"account_id"} | 400 invalid_username invalid_nickname invalid_password invalid_email captcha_failed invalid_request
           | 409 username_taken nickname_taken | 429 rate_limited | 500 server_error
        */
        void HttpSession::handleAccountRegister(std::string& responseBody, http::status& statusCode)
        {
            if (apiRateLimited(registerLimiter(), "register", responseBody, statusCode))
            {
                return;
            }

            const auto fail = [&](http::status status, const char* code) {
                apiEvent("account_register", "LOW", 0, "fail", code);
                apiError(responseBody, statusCode, status, code);
            };

            boost::json::object body;
            if (!apiParseObject(body))
            {
                return fail(http::status::bad_request, "invalid_request");
            }

            std::string username, nickname, password, email, turnstile;
            if (!getString(body, "username", username) || !Api::validUsername(username))
            {
                return fail(http::status::bad_request, "invalid_username");
            }
            if (!getString(body, "nickname", nickname) || !Api::validNickname(nickname))
            {
                return fail(http::status::bad_request, "invalid_nickname");
            }
            if (!getString(body, "password", password) || !Api::validPassword(password, username))
            {
                return fail(http::status::bad_request, "invalid_password");
            }
            if (!getString(body, "email", email) || !Api::validEmail(email))
            {
                return fail(http::status::bad_request, "invalid_email");
            }

            const auto& website = Common::Utils::SetupParser::getInstance().getWebsiteSetup();
            if (!website.turnstileSecret.empty())
            {
                getString(body, "turnstile", turnstile);
                if (Turnstile::verify(website.turnstileSecret, turnstile, clientIp()) != Turnstile::Verdict::Passed)
                {
                    return fail(http::status::bad_request, "captcha_failed");
                }
            }

            const auto encryptedEmail = Common::Utils::encryptEmail(email, Common::Utils::SetupParser::getInstance().getGeneralSetup().emailSecret);
            if (!encryptedEmail)
            {
                ::Utils::Logger::log("Could not encrypt the email of a new account", ::Utils::LogType::Error, "HttpSession::handleAccountRegister");
                return fail(http::status::internal_server_error, "server_error");
            }
            const std::string hash = Common::Utils::hashPassword(password);

            const auto result = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::registerWebAccount,
                username, hash, nickname, *encryptedEmail);
            switch (result)
            {
            case Main::Structures::WebRegisterResult::UsernameTaken:
                return fail(http::status::conflict, "username_taken");
            case Main::Structures::WebRegisterResult::NicknameTaken:
                return fail(http::status::conflict, "nickname_taken");
            case Main::Structures::WebRegisterResult::Error:
                return fail(http::status::internal_server_error, "server_error");
            case Main::Structures::WebRegisterResult::Created:
                break;
            }

            const WebAccountLookup created = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::getWebAccountByUsername, username);
            if (created.status != WebAccountLookup::Status::Found)
            {
                ::Utils::Logger::log("The new account could not be read back", ::Utils::LogType::Error, "HttpSession::handleAccountRegister");
                return fail(http::status::internal_server_error, "server_error");
            }
            apiEvent("account_register", "LOW", created.account.accountId, "ok", "created");
            boost::json::object out;
            out["account_id"] = created.account.accountId;
            statusCode = http::status::created;
            responseBody = boost::json::serialize(out);
        }

        /*
           POST /account/login (public) {"username","password","totp"?,"turnstile"?}
           200 {"token","expires_in":3600,"account":{...}} | 400 invalid_request | 401 invalid_credentials totp_required | 403 suspended | 429 rate_limited
        */
        void HttpSession::handleAccountLogin(std::string& responseBody, http::status& statusCode)
        {
            if (apiRateLimited(loginLimiter(), "login", responseBody, statusCode))
            {
                return;
            }

            boost::json::object body;
            std::string username, password, totp;
            if (!apiParseObject(body) || !getString(body, "username", username) || !getString(body, "password", password)
                || username.empty() || username.size() > 64 || password.empty() || password.size() > Api::maxPasswordLength * 4)
            {
                apiEvent("authn_fail", "LOW", 0, "fail", "login_invalid_request");
                return apiError(responseBody, statusCode, http::status::bad_request, "invalid_request");
            }
            if (const auto* value = field(body, "totp"))
            {
                if (!value->is_string() || value->as_string().size() > 64)
                {
                    apiEvent("authn_fail", "LOW", 0, "fail", "login_invalid_request");
                    return apiError(responseBody, statusCode, http::status::bad_request, "invalid_request");
                }
                totp = std::string(value->as_string());
            }

            const auto badCredentials = [&](std::uint32_t accountId, const char* reason) {
                apiEvent("authn_fail", "LOW", accountId, "fail", reason);
                apiError(responseBody, statusCode, http::status::unauthorized, "invalid_credentials");
            };

            const WebAccountLookup lookup = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::getWebAccountByUsername, username);
            if (lookup.status == WebAccountLookup::Status::Error)
            {
                apiEvent("authn_fail", "MEDIUM", 0, "fail", "database_error");
                return apiError(responseBody, statusCode, http::status::internal_server_error, "server_error");
            }
            if (lookup.status == WebAccountLookup::Status::NotFound)
            {
                Common::Utils::verifyPassword(password, dummyPasswordHash());
                return badCredentials(0, "unknown_account");
            }

            const WebAccount& account = lookup.account;
            const bool passwordOk = Common::Utils::verifyPassword(password, account.passwordHash);
            if (!passwordOk)
            {
                return badCredentials(account.accountId, "bad_password");
            }
            if (account.grade >= static_cast<std::int32_t>(firstStaffGrade))
            {
                // Staff sign in through the game's graded login (own port, hardware id, lockout), not with a password on the open website
                return badCredentials(account.accountId, "staff_account_refused");
            }

            if (account.twoFactorEnabled)
            {
                switch (apiCheckSecondFactor(account, totp))
                {
                case SecondFactor::Valid:
                    break;
                case SecondFactor::Missing:
                    apiEvent("authn_fail", "LOW", account.accountId, "fail", "totp_required");
                    return apiError(responseBody, statusCode, http::status::unauthorized, "totp_required");
                case SecondFactor::Locked:
                    return badCredentials(account.accountId, "2fa_locked");
                case SecondFactor::Replayed:
                    apiEvent("authn_fail", "HIGH", account.accountId, "fail", "2fa_code_replayed");
                    return apiError(responseBody, statusCode, http::status::unauthorized, "invalid_credentials");
                case SecondFactor::Invalid:
                    return badCredentials(account.accountId, "bad_2fa_code");
                }
            }

            m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::resetFailedLogins, account.accountId);

            if (Common::Utils::datetimeToEpoch(account.suspendedUntil) > static_cast<std::uint64_t>(std::time(nullptr)))
            {
                apiEvent("authn_fail", "LOW", account.accountId, "denied", "suspended");
                return apiError(responseBody, statusCode, http::status::forbidden, "suspended");
            }

            if (Common::Utils::passwordNeedsUpgrade(account.passwordHash))
            {
                m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::replacePasswordHash,
                    account.accountId, account.passwordHash, Common::Utils::hashPassword(password));
            }

            const auto& website = Common::Utils::SetupParser::getInstance().getWebsiteSetup();
            boost::json::object accountJson;
            accountJson["id"] = account.accountId;
            accountJson["username"] = account.username;
            accountJson["nickname"] = account.nickname;
            accountJson["grade"] = account.grade;
            accountJson["twofa"] = account.twoFactorEnabled;
            boost::json::object out;
            out["token"] = Common::Utils::WebToken::issue(account.accountId, account.grade, website.jwtToken);
            out["expires_in"] = Common::Utils::WebToken::lifetime.count();
            out["account"] = std::move(accountJson);
            apiEvent("authn_ok", "LOW", account.accountId, "ok", "web_login");
            statusCode = http::status::ok;
            responseBody = boost::json::serialize(out);
        }

        /*
           GET /account/me (Bearer)
           200 {"id","username","nickname","grade","twofa","level","kills","deaths","wins","loses","playtime"} | 401 unauthorized
        */
        void HttpSession::handleAccountMe(std::string& responseBody, http::status& statusCode)
        {
            const auto claims = apiBearer();
            if (!claims)
            {
                return apiError(responseBody, statusCode, http::status::unauthorized, "unauthorized");
            }
            const WebAccountLookup lookup = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::getWebAccountById, claims->accountId);
            if (lookup.status == WebAccountLookup::Status::NotFound)
            {
                return apiError(responseBody, statusCode, http::status::unauthorized, "unauthorized");
            }
            if (lookup.status == WebAccountLookup::Status::Error)
            {
                return apiError(responseBody, statusCode, http::status::internal_server_error, "server_error");
            }
            const WebAccount& a = lookup.account;
            boost::json::object out;
            out["id"] = a.accountId;
            out["username"] = a.username;
            out["nickname"] = a.nickname;
            out["grade"] = a.grade;
            out["twofa"] = a.twoFactorEnabled;
            out["level"] = a.level;
            out["kills"] = a.kills;
            out["deaths"] = a.deaths;
            out["wins"] = a.wins;
            out["loses"] = a.loses;
            out["playtime"] = a.playtime;
            statusCode = http::status::ok;
            responseBody = boost::json::serialize(out);
        }

        /*
           POST /account/password (Bearer) {"current_password","new_password","totp"?}
           204 | 400 invalid_password invalid_request | 401 unauthorized invalid_credentials totp_required | 429 rate_limited | 500 server_error
        */
        void HttpSession::handleAccountPassword(std::string& responseBody, http::status& statusCode)
        {
            if (apiRateLimited(passwordLimiter(), "password", responseBody, statusCode))
            {
                return;
            }
            const auto claims = apiBearer();
            if (!claims)
            {
                apiEvent("password_change", "LOW", 0, "denied", "no_valid_token");
                return apiError(responseBody, statusCode, http::status::unauthorized, "unauthorized");
            }

            boost::json::object body;
            std::string current, next, totp;
            if (!apiParseObject(body) || !getString(body, "current_password", current) || current.size() > Api::maxPasswordLength * 4)
            {
                apiEvent("password_change", "LOW", claims->accountId, "fail", "invalid_request");
                return apiError(responseBody, statusCode, http::status::bad_request, "invalid_request");
            }
            if (const auto* value = field(body, "totp"))
            {
                if (!value->is_string() || value->as_string().size() > 64)
                {
                    apiEvent("password_change", "LOW", claims->accountId, "fail", "invalid_request");
                    return apiError(responseBody, statusCode, http::status::bad_request, "invalid_request");
                }
                totp = std::string(value->as_string());
            }

            const WebAccountLookup lookup = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::getWebAccountById, claims->accountId);
            if (lookup.status == WebAccountLookup::Status::NotFound)
            {
                apiEvent("password_change", "LOW", claims->accountId, "denied", "unknown_account");
                return apiError(responseBody, statusCode, http::status::unauthorized, "unauthorized");
            }
            if (lookup.status == WebAccountLookup::Status::Error)
            {
                return apiError(responseBody, statusCode, http::status::internal_server_error, "server_error");
            }
            const WebAccount& account = lookup.account;

            if (!getString(body, "new_password", next) || !Api::validPassword(next, account.username))
            {
                apiEvent("password_change", "LOW", account.accountId, "fail", "invalid_password");
                return apiError(responseBody, statusCode, http::status::bad_request, "invalid_password");
            }

            if (!Common::Utils::verifyPassword(current, account.passwordHash))
            {
                apiEvent("password_change", "MEDIUM", account.accountId, "fail", "bad_current_password");
                return apiError(responseBody, statusCode, http::status::unauthorized, "invalid_credentials");
            }

            if (account.twoFactorEnabled)
            {
                switch (apiCheckSecondFactor(account, totp))
                {
                case SecondFactor::Valid:
                    break;
                case SecondFactor::Missing:
                    apiEvent("password_change", "LOW", account.accountId, "fail", "totp_required");
                    return apiError(responseBody, statusCode, http::status::unauthorized, "totp_required");
                case SecondFactor::Replayed:
                    apiEvent("password_change", "HIGH", account.accountId, "fail", "2fa_code_replayed");
                    return apiError(responseBody, statusCode, http::status::unauthorized, "invalid_credentials");
                case SecondFactor::Locked:
                case SecondFactor::Invalid:
                    apiEvent("password_change", "MEDIUM", account.accountId, "fail", "bad_2fa_code");
                    return apiError(responseBody, statusCode, http::status::unauthorized, "invalid_credentials");
                }
            }

            const bool replaced = m_scheduler.immediatePersist(std::source_location::current(), &Main::Persistence::PersistentDatabase::replacePasswordHash,
                account.accountId, account.passwordHash, Common::Utils::hashPassword(next));
            if (!replaced)
            {
                apiEvent("password_change", "MEDIUM", account.accountId, "fail", "could_not_store");
                return apiError(responseBody, statusCode, http::status::internal_server_error, "server_error");
            }
            apiEvent("password_change", "MEDIUM", account.accountId, "ok", "changed");
            statusCode = http::status::no_content;
            responseBody.clear();
        }

        // Routes /account/*. False when the target is not an account endpoint.
        bool HttpSession::handleAccountRequest(std::string& responseBody, http::status& statusCode)
        {
            const std::string_view target = m_request.target();
            if (!target.starts_with("/account/"))
            {
                return false;
            }

            m_jsonResponse = true;
            const auto verb = m_request.method();
            struct Route { std::string_view path; http::verb method; void (HttpSession::*handler)(std::string&, http::status&); };
            static constexpr Route routes[] = {
                { "/account/status", http::verb::get, &HttpSession::handleAccountStatus },
                { "/account/me", http::verb::get, &HttpSession::handleAccountMe },
                { "/account/register", http::verb::post, &HttpSession::handleAccountRegister },
                { "/account/login", http::verb::post, &HttpSession::handleAccountLogin },
                { "/account/password", http::verb::post, &HttpSession::handleAccountPassword },
            };

            bool pathKnown = false;
            for (const Route& route : routes)
            {
                if (route.path != target)
                {
                    continue;
                }
                pathKnown = true;
                if (route.method == verb)
                {
                    (this->*route.handler)(responseBody, statusCode);
                    return true;
                }
            }
            apiError(responseBody, statusCode, pathKnown ? http::status::method_not_allowed : http::status::not_found, pathKnown ? "method_not_allowed" : "not_found");
            return true;
        }
    }
}
