#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "Utils/SecretSource.h"

namespace Secret = Common::Utils::SecretSource;

namespace
{
	// Fake values only
	const std::string fakeValue = "fake-secret-value-0123456789abcdef";

	class SecretSourceTest : public ::testing::Test
	{
	protected:
		std::map<std::string, std::string> variables;
		std::filesystem::path directory;

		void SetUp() override
		{
			directory = std::filesystem::temp_directory_path() / ("secret_source_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(directory);
		}

		void TearDown() override
		{
			std::error_code ignored;
			std::filesystem::remove_all(directory, ignored);
		}

		Secret::Lookup lookup()
		{
			return [this](const char* name) -> const char* {
				const auto found = variables.find(name);
				return found == variables.end() ? nullptr : found->second.c_str();
			};
		}

		std::string writeFile(const std::string& name, const std::string& content)
		{
			const std::filesystem::path path = directory / name;
			std::ofstream(path, std::ios::binary) << content;
			return path.string();
		}
	};
}

TEST_F(SecretSourceTest, NeitherSetIsNotSet)
{
	const Secret::Result result = Secret::read("MV_TEST", lookup());
	EXPECT_EQ(result.origin, Secret::Origin::NotSet);
	EXPECT_FALSE(result.ok());
}

TEST_F(SecretSourceTest, EnvironmentValueIsReturnedUnchanged)
{
	variables["MV_TEST"] = fakeValue + "\n";
	const Secret::Result result = Secret::read("MV_TEST", lookup());
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(result.origin, Secret::Origin::Environment);
	EXPECT_EQ(result.value, fakeValue + "\n"); // the environment path keeps its old behaviour
}

TEST_F(SecretSourceTest, FileValueIsReadAndOneTrailingNewlineRemoved)
{
	variables["MV_TEST_FILE"] = writeFile("a", fakeValue + "\n");
	const Secret::Result result = Secret::read("MV_TEST", lookup());
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(result.origin, Secret::Origin::File);
	EXPECT_EQ(result.value, fakeValue);
}

TEST_F(SecretSourceTest, WindowsNewlineIsRemovedToo)
{
	variables["MV_TEST_FILE"] = writeFile("a", fakeValue + "\r\n");
	EXPECT_EQ(Secret::read("MV_TEST", lookup()).value, fakeValue);
}

TEST_F(SecretSourceTest, OnlyOneNewlineIsRemoved)
{
	variables["MV_TEST_FILE"] = writeFile("a", fakeValue + "\n\n");
	EXPECT_EQ(Secret::read("MV_TEST", lookup()).value, fakeValue + "\n");
}

TEST_F(SecretSourceTest, FileWithoutNewlineAndOuterSpacesIsKept)
{
	variables["MV_TEST_FILE"] = writeFile("a", " " + fakeValue + " ");
	EXPECT_EQ(Secret::read("MV_TEST", lookup()).value, " " + fakeValue + " ");
}

TEST_F(SecretSourceTest, BothSetIsRefused)
{
	variables["MV_TEST"] = fakeValue;
	variables["MV_TEST_FILE"] = writeFile("a", fakeValue);
	const Secret::Result result = Secret::read("MV_TEST", lookup());
	EXPECT_EQ(result.origin, Secret::Origin::Error);
	EXPECT_FALSE(result.ok());
	EXPECT_TRUE(result.value.empty());
	EXPECT_NE(result.error.find("both set"), std::string::npos);
}

TEST_F(SecretSourceTest, MissingFileFailsClosed)
{
	variables["MV_TEST_FILE"] = (directory / "does_not_exist").string();
	const Secret::Result result = Secret::read("MV_TEST", lookup());
	EXPECT_EQ(result.origin, Secret::Origin::Error);
	EXPECT_TRUE(result.value.empty());
	EXPECT_NE(result.error.find("MV_TEST_FILE"), std::string::npos);
}

TEST_F(SecretSourceTest, DirectoryInsteadOfFileFailsClosed)
{
	variables["MV_TEST_FILE"] = directory.string();
	const Secret::Result result = Secret::read("MV_TEST", lookup());
	EXPECT_EQ(result.origin, Secret::Origin::Error);
	EXPECT_TRUE(result.value.empty());
}

TEST_F(SecretSourceTest, EmptyFileFailsClosed)
{
	variables["MV_TEST_FILE"] = writeFile("empty", "");
	const Secret::Result result = Secret::read("MV_TEST", lookup());
	EXPECT_EQ(result.origin, Secret::Origin::Error);
	EXPECT_NE(result.error.find("empty"), std::string::npos);
}

TEST_F(SecretSourceTest, FileWithOnlyANewlineFailsClosed)
{
	variables["MV_TEST_FILE"] = writeFile("newline", "\n");
	EXPECT_EQ(Secret::read("MV_TEST", lookup()).origin, Secret::Origin::Error);
}

TEST_F(SecretSourceTest, EmptyFilePathFailsClosed)
{
	variables["MV_TEST_FILE"] = "";
	EXPECT_EQ(Secret::read("MV_TEST", lookup()).origin, Secret::Origin::Error);
}

TEST_F(SecretSourceTest, ErrorMessagesNeverContainTheValue)
{
	variables["MV_TEST"] = fakeValue;
	variables["MV_TEST_FILE"] = writeFile("a", fakeValue);
	EXPECT_EQ(Secret::read("MV_TEST", lookup()).error.find(fakeValue), std::string::npos);

	variables.erase("MV_TEST");
	variables["MV_TEST_FILE"] = writeFile("empty", "");
	EXPECT_EQ(Secret::read("MV_TEST", lookup()).error.find(fakeValue), std::string::npos);
}
