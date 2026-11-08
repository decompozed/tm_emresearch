#ifndef MAIN_CONTROL_ANNOUNCE_PARSER_H
#define MAIN_CONTROL_ANNOUNCE_PARSER_H

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace Main
{
	namespace Control
	{
		enum class AnnounceKind : std::uint8_t { Announce, Tip };

		// 402 opt 0x0A = annuncio del client, opt 0x0C = tip; payload testo, max 512 byte
		inline constexpr std::size_t kMaxAnnounceBytes = 512;

		struct AnnounceCommand
		{
			enum class Status : std::uint8_t { Ok, Empty, TooLong };

			AnnounceKind kind = AnnounceKind::Announce;
			std::string message;
			Status status = Status::Empty;
		};

		inline std::string_view trim(std::string_view text)
		{
			constexpr std::string_view blanks = " \t\r\n";
			const std::size_t first = text.find_first_not_of(blanks);
			if (first == std::string_view::npos)
			{
				return {};
			}
			const std::size_t last = text.find_last_not_of(blanks);
			return text.substr(first, last - first + 1);
		}

		inline AnnounceCommand parseAnnounceControl(std::string_view content)
		{
			AnnounceCommand result;

			const std::size_t newline = content.find('\n');
			const std::string_view firstLine = content.substr(0, newline);
			const std::string_view rest = newline == std::string_view::npos ? std::string_view{} : content.substr(newline + 1);

			std::string type{ trim(firstLine) };
			for (char& c : type)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			result.kind = type == "tip" ? AnnounceKind::Tip : AnnounceKind::Announce;

			result.message = std::string{ trim(rest) };
			if (result.message.empty())
			{
				result.status = AnnounceCommand::Status::Empty;
			}
			else if (result.message.size() > kMaxAnnounceBytes)
			{
				result.status = AnnounceCommand::Status::TooLong;
			}
			else
			{
				result.status = AnnounceCommand::Status::Ok;
			}
			return result;
		}
	}
}

#endif
