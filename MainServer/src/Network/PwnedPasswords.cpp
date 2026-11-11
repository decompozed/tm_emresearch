#include "Network/PwnedPasswords.h"

#include <curl/curl.h>

#include "Utils/Logger.h"
#include "Utils/PwnedPasswords.h"

namespace Main
{
	namespace Network
	{
		namespace PwnedPasswords
		{
			namespace
			{
				namespace Range = Common::Utils::PwnedPasswords;

				constexpr const char* rangeUrl = "https://api.pwnedpasswords.com/range/";
				constexpr std::size_t maxResponseBytes = 256 * 1024; // a padded range is about 40 KB

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

			Verdict check(const std::string& password)
			{
				const std::string hash = Range::sha1Hex(password);
				const std::string url = rangeUrl + hash.substr(0, Range::prefixLength);

				CURL* curl = curl_easy_init();
				if (!curl)
				{
					return Verdict::Unavailable;
				}

				std::string response;
				curl_slist* headers = curl_slist_append(nullptr, "Add-Padding: true");
				curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
				curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				curl_easy_setopt(curl, CURLOPT_USERAGENT, "toymayhem-account-api");
				curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
				curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
				curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
				curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
				curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
				curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

				const CURLcode code = curl_easy_perform(curl);
				long status = 0;
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
				curl_slist_free_all(headers);
				curl_easy_cleanup(curl);

				if (code != CURLE_OK)
				{
					::Utils::Logger::log(std::string("Pwned Passwords range request failed, the password was not checked: ") + curl_easy_strerror(code),
						::Utils::LogType::Warning, "PwnedPasswords::check");
					return Verdict::Unavailable;
				}
				if (status != 200)
				{
					::Utils::Logger::log("Pwned Passwords answered HTTP " + std::to_string(status) + ", the password was not checked",
						::Utils::LogType::Warning, "PwnedPasswords::check");
					return Verdict::Unavailable;
				}

				const auto count = Range::countInRange(response, std::string_view(hash).substr(Range::prefixLength));
				if (!count)
				{
					::Utils::Logger::log("Pwned Passwords answered something that is not a range, the password was not checked",
						::Utils::LogType::Warning, "PwnedPasswords::check");
					return Verdict::Unavailable;
				}
				return *count > 0 ? Verdict::Breached : Verdict::NotFound;
			}
		}
	}
}
