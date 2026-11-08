#ifndef COMMON_UTILS_SECRET_SOURCE_H
#define COMMON_UTILS_SECRET_SOURCE_H

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>

// Where a secret comes from: the environment variable NAME, or a file whose path is in NAME_FILE (the Docker secrets
// convention, so a secret can be mounted as a file instead of showing up in "docker inspect").
// Only the logic lives here, so it can be tested without a server. The value is never part of an error message.
namespace Common
{
	namespace Utils
	{
		namespace SecretSource
		{
			using Lookup = std::function<const char*(const char*)>;

			enum class Origin
			{
				NotSet,       // neither NAME nor NAME_FILE is set
				Environment,
				File,
				Error         // see Result::error
			};

			struct Result
			{
				Origin origin = Origin::NotSet;
				std::string value;
				std::string error;   // names the variable or file path, never the value

				bool ok() const { return origin == Origin::Environment || origin == Origin::File; }
			};

			inline const char* defaultLookup(const char* name)
			{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // getenv: the value is copied right away by read()
#endif
				return std::getenv(name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
			}

			// Removes one trailing newline ("\n" or "\r\n"), as an editor or "echo" leaves it. Other whitespace stays part of the secret.
			inline std::string trimOneTrailingNewline(std::string text)
			{
				if (!text.empty() && text.back() == '\n')
				{
					text.pop_back();
					if (!text.empty() && text.back() == '\r')
					{
						text.pop_back();
					}
				}
				return text;
			}

			inline Result read(const std::string& name, const Lookup& lookup = defaultLookup)
			{
				Result result;
				const std::string fileVariable = name + "_FILE";
				const char* envValue = lookup(name.c_str());
				const char* filePath = lookup(fileVariable.c_str());

				if (envValue != nullptr && filePath != nullptr)
				{
					result.origin = Origin::Error;
					result.error = name + " and " + fileVariable + " are both set: use only one";
					return result;
				}
				if (envValue != nullptr)
				{
					result.origin = Origin::Environment;
					result.value = envValue;
					return result;
				}
				if (filePath == nullptr)
				{
					return result;
				}

				result.origin = Origin::Error;
				if (*filePath == '\0')
				{
					result.error = fileVariable + " is set but empty";
					return result;
				}
				std::ifstream file(filePath, std::ios::binary);
				if (!file)
				{
					result.error = std::string("Cannot open the secret file named by ") + fileVariable;
					return result;
				}
				std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
				if (file.bad())
				{
					result.error = std::string("Cannot read the secret file named by ") + fileVariable;
					return result;
				}
				content = trimOneTrailingNewline(std::move(content));
				if (content.empty())
				{
					result.error = std::string("The secret file named by ") + fileVariable + " is empty";
					return result;
				}
				result.origin = Origin::File;
				result.value = std::move(content);
				return result;
			}
		}
	}
}

#endif
