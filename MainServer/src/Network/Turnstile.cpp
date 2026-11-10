#include "Network/Turnstile.h"

#include <curl/curl.h>
#include <boost/json.hpp>

#include "Utils/AccountApi.h"
#include "Utils/Logger.h"

namespace Main
{
	namespace Network
	{
		namespace Turnstile
		{
			namespace
			{
				constexpr const char* siteVerifyUrl = "https://challenges.cloudflare.com/turnstile/v0/siteverify";
				constexpr std::size_t maxResponseBytes = 16 * 1024;

				std::size_t collect(char* data, std::size_t size, std::size_t count, void* userdata)
				{
					auto* out = static_cast<std::string*>(userdata);
					const std::size_t bytes = size * count;
					if (out->size() + bytes > maxResponseBytes)
					{
						return 0; // too much: curl stops the transfer
					}
					out->append(data, bytes);
					return bytes;
				}
			}

			Verdict verify(const std::string& secret, const std::string& token, const std::string& remoteIp)
			{
				if (token.empty() || token.size() > Common::Utils::AccountApi::maxTurnstileTokenLength)
				{
					return Verdict::Rejected;
				}

				CURL* curl = curl_easy_init();
				if (!curl)
				{
					return Verdict::Unreachable;
				}

				std::string response;
				char* escapedSecret = curl_easy_escape(curl, secret.c_str(), static_cast<int>(secret.size()));
				char* escapedToken = curl_easy_escape(curl, token.c_str(), static_cast<int>(token.size()));
				char* escapedIp = curl_easy_escape(curl, remoteIp.c_str(), static_cast<int>(remoteIp.size()));
				std::string form;
				if (escapedSecret && escapedToken && escapedIp)
				{
					form = std::string("secret=") + escapedSecret + "&response=" + escapedToken + "&remoteip=" + escapedIp;
				}
				curl_free(escapedSecret);
				curl_free(escapedToken);
				curl_free(escapedIp);
				if (form.empty())
				{
					curl_easy_cleanup(curl);
					return Verdict::Unreachable;
				}

				curl_easy_setopt(curl, CURLOPT_URL, siteVerifyUrl);
				curl_easy_setopt(curl, CURLOPT_POST, 1L);
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
				curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
				curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
				curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
				curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
				curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
				curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

				const CURLcode code = curl_easy_perform(curl);
				long status = 0;
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
				curl_easy_cleanup(curl);

				if (code != CURLE_OK)
				{
					::Utils::Logger::log(std::string("Turnstile siteverify failed: ") + curl_easy_strerror(code), ::Utils::LogType::Warning, "Turnstile::verify");
					return Verdict::Unreachable;
				}
				if (status != 200)
				{
					::Utils::Logger::log("Turnstile siteverify answered HTTP " + std::to_string(status), ::Utils::LogType::Warning, "Turnstile::verify");
					return Verdict::Unreachable;
				}

				boost::system::error_code ec;
				const boost::json::value parsed = boost::json::parse(response, ec);
				if (ec || !parsed.is_object())
				{
					::Utils::Logger::log("Turnstile siteverify answered something that is not a JSON object", ::Utils::LogType::Warning, "Turnstile::verify");
					return Verdict::Unreachable;
				}
				const boost::json::value* success = parsed.as_object().if_contains("success");
				return (success && success->is_bool() && success->as_bool()) ? Verdict::Passed : Verdict::Rejected;
			}
		}
	}
}
