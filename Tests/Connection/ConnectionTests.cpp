#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <initializer_list>
#include <cstring>
#include <vector>
#include "Connection/ConnectionRules.h"
#include "HaveApi/CastConnect.h"
#include "Network/SessionIdManager.h"
#include "Structures/Connection/ConnectionPackets.h"

using namespace Cast::Connection;
using Cast::Structures::ConnectRequest;
using Cast::Structures::UniqueId;
using Common::HaveApi::CastConnectAnswer;
using Common::HaveApi::CastConnectCheck;
using Common::HaveApi::CastConnectVerdict;

namespace
{
	constexpr std::uint16_t victimSession = 498;
	constexpr std::uint32_t victimAccount = 1234;
	constexpr std::uint32_t victimHash = 0x5A5A1234;

	ConnectRequest request(std::uint16_t session = victimSession, std::uint32_t account = victimAccount, std::uint32_t hash = victimHash)
	{
		ConnectRequest connect{};
		connect.playerKey = UniqueId{ session, 1, 0 };
		connect.accountId = account;
		connect.hashKey = hash;
		return connect;
	}

	std::vector<std::uint8_t> bytes(const ConnectRequest& connect)
	{
		std::vector<std::uint8_t> data(sizeof(connect));
		std::memcpy(data.data(), &connect, sizeof(connect));
		return data;
	}

	Command command(std::uint16_t order, const std::vector<std::uint8_t>& payload, std::uint8_t mission = 0, std::uint8_t extra = 0, std::uint8_t option = 0)
	{
		return { order, mission, extra, option, { payload.data(), payload.size() } };
	}

	CastConnectVerdict mainVerdict(const ConnectRequest& connect)
	{
		return Common::HaveApi::verifyCastConnect(toCheck(connect), victimHash, std::bit_cast<std::uint32_t>(UniqueId{ victimSession, 1, 0 }));
	}

	CastConnectAnswer accepted(std::uint16_t session = victimSession)
	{
		return { session, CastConnectVerdict::Accepted, 0 };
	}
}

TEST(CastConnect, ClientRequestIsParsed)
{
	const auto data = bytes(request());
	const auto parsed = parseConnect(command(252, data));
	ASSERT_TRUE(parsed);
	EXPECT_EQ(parsed->playerKey.session, victimSession);
	EXPECT_EQ(parsed->accountId, victimAccount);
	EXPECT_EQ(parsed->hashKey, victimHash);
}

TEST(CastConnect, RequestOutsideTheClientLayoutIsRefused)
{
	auto data = bytes(request());
	EXPECT_FALSE(parseConnect(command(252, data, 1)));
	EXPECT_FALSE(parseConnect(command(252, data, 0, 1)));
	EXPECT_FALSE(parseConnect(command(252, data, 0, 0, 1)));
	data.push_back(0);
	EXPECT_FALSE(parseConnect(command(252, data)));
	EXPECT_FALSE(parseConnect(command(252, bytes(request(victimSession, 0)))));
	EXPECT_FALSE(parseConnect(command(252, bytes(request(victimSession, victimAccount, 0)))));
	auto flagged = request();
	flagged.playerKey.unknown = 1;
	EXPECT_FALSE(parseConnect(command(252, bytes(flagged))));
}

TEST(CastConnect, MainServerAcceptsTheLoggedInPlayer)
{
	EXPECT_EQ(mainVerdict(request()), CastConnectVerdict::Accepted);
}

TEST(CastConnect, ForgedRequestsAreRefusedByTheMainServer)
{
	EXPECT_EQ(mainVerdict(request(victimSession, victimAccount, victimHash + 1)), CastConnectVerdict::WrongHashKey);
	EXPECT_EQ(mainVerdict(request(victimSession, victimAccount, 0)), CastConnectVerdict::WrongHashKey);
	EXPECT_EQ(mainVerdict(request(victimSession - 1)), CastConnectVerdict::WrongPlayerKey);
	EXPECT_EQ(Common::HaveApi::verifyCastConnect(toCheck(request()), 0, std::bit_cast<std::uint32_t>(UniqueId{ victimSession, 1, 0 })),
		CastConnectVerdict::WrongHashKey);
}

TEST(CastConnect, PlayerKeyComparedOn31Bits)
{
	EXPECT_EQ(Common::HaveApi::verifyCastConnect(toCheck(request()), victimHash, std::bit_cast<std::uint32_t>(UniqueId{ victimSession, 1, 1 })),
		CastConnectVerdict::Accepted);
}

TEST(CastConnect, OnlyAnAcceptedAnswerForTheRequestedSessionAuthenticates)
{
	const auto connect = request();
	EXPECT_EQ(verifiedSession(accepted(), connect), victimSession);
	EXPECT_FALSE(verifiedSession(std::nullopt, connect));
	EXPECT_FALSE(verifiedSession(CastConnectAnswer{ victimSession, CastConnectVerdict::WrongHashKey, 0 }, connect));
	EXPECT_FALSE(verifiedSession(accepted(victimSession - 1), connect));
	EXPECT_FALSE(verifiedSession(accepted(0), request(0)));
	EXPECT_FALSE(verifiedSession(accepted(Common::Constants::maxSessionsPerServer + 1), request(Common::Constants::maxSessionsPerServer + 1)));
}

TEST(CastConnect, AnswerDecodedOnlyWithItsExactSize)
{
	const CastConnectAnswer answer = accepted();
	std::array<std::uint8_t, sizeof(answer) + 1> data{};
	std::memcpy(data.data(), &answer, sizeof(answer));
	EXPECT_TRUE(Common::HaveApi::decodeExact<CastConnectAnswer>(data.data(), sizeof(answer)));
	EXPECT_FALSE(Common::HaveApi::decodeExact<CastConnectAnswer>(data.data(), data.size()));
	EXPECT_FALSE(Common::HaveApi::decodeExact<CastConnectAnswer>(nullptr, sizeof(answer)));
}

TEST(CastConnect, OutcomeOfTheMainServerAnswer)
{
	EXPECT_EQ(connectOutcome(true, Stage::Verifying, victimSession, false), Outcome::Register);
	EXPECT_EQ(connectOutcome(true, Stage::Verifying, victimSession, true), Outcome::ReplaceHolder);
	EXPECT_EQ(connectOutcome(true, Stage::Verifying, std::nullopt, false), Outcome::Reject);
}

TEST(CastConnect, ClosedSocketIsNeverRegistered)
{
	EXPECT_EQ(connectOutcome(false, Stage::Verifying, victimSession, false), Outcome::Ignore);
	EXPECT_EQ(connectOutcome(false, Stage::Verifying, std::nullopt, false), Outcome::Ignore);
}

TEST(CastConnect, AnswerForAnAuthenticatedSocketIsIgnored)
{
	EXPECT_EQ(connectOutcome(true, Stage::Authenticated, victimSession, false), Outcome::Ignore);
	EXPECT_EQ(connectOutcome(true, Stage::AwaitingConnect, victimSession, false), Outcome::Ignore);
}

TEST(ConnectionGate, OnlyTheConnectBeforeAuthentication)
{
	EXPECT_TRUE(accepts(Stage::AwaitingConnect, 252));
	for (const std::uint16_t order : std::initializer_list<std::uint16_t>{ 71, 253, 277, 140, 279, 281, 264, 257 })
	{
		EXPECT_FALSE(accepts(Stage::AwaitingConnect, order));
		EXPECT_FALSE(accepts(Stage::Verifying, order));
		EXPECT_TRUE(accepts(Stage::Authenticated, order));
	}
}

TEST(ConnectionGate, SecondConnectIsRefused)
{
	EXPECT_FALSE(accepts(Stage::Verifying, 252));
	EXPECT_FALSE(accepts(Stage::Authenticated, 252));
}

TEST(Ping, ClientLayout)
{
	const std::vector<std::uint8_t> none;
	EXPECT_TRUE(isPing(command(71, none, 1)));
	EXPECT_TRUE(isPing(command(71, none, 1, 0, 3)));
	EXPECT_FALSE(isPing(command(71, none, 0)));
	EXPECT_FALSE(isPing(command(71, none, 1, 1)));
	EXPECT_FALSE(isPing(command(71, { 0, 0, 0, 0 }, 1)));
}

TEST(Ping, PongIsHeaderOnlyMission1)
{
	EXPECT_EQ(Cast::Structures::pong.order, 72);
	EXPECT_EQ(Cast::Structures::pong.mission, 1);
	EXPECT_EQ(Cast::Structures::pong.extra, 0);
	EXPECT_EQ(Cast::Structures::pong.option, 0);
	EXPECT_EQ(Cast::Structures::castConnected.order, 501);
	EXPECT_EQ(Cast::Structures::castConnected.extra, 32);
}

TEST(CastJoin, DeniedIsHeaderOnlyExtra35)
{
	EXPECT_EQ(Cast::Structures::castJoinDenied.order, 278);
	EXPECT_EQ(Cast::Structures::castJoinDenied.mission, 0);
	EXPECT_EQ(Cast::Structures::castJoinDenied.extra, 35);
	EXPECT_EQ(Cast::Structures::castJoinDenied.option, 0);
}

TEST(Crash, ClientLayout)
{
	const std::vector<std::uint8_t> none;
	EXPECT_TRUE(isCrashReport(command(253, none, 0, 41)));
	EXPECT_TRUE(isCrashReport(command(253, none, 0, 2)));
	EXPECT_FALSE(isCrashReport(command(253, { 1, 0, 0, 0 }, 0, 41)));
	EXPECT_FALSE(isCrashReport(command(253, none, 1, 41)));
	EXPECT_FALSE(isCrashReport(command(253, none, 0, 41, 1)));
}

TEST(SessionIds, CastLocalIdsNeverOverlapMainServerIds)
{
	SessionIdManager main{ 1, Common::Constants::maxSessionsPerServer };
	SessionIdManager cast{ firstLocalSessionId, localSessionIds };
	EXPECT_EQ(main.first(), 1u);
	EXPECT_EQ(main.last(), std::size_t{ Common::Constants::maxSessionsPerServer });
	EXPECT_GT(cast.first(), main.last());
	EXPECT_LE(cast.last(), 0x3FFFu);
	for (std::size_t i = 0; i < localSessionIds; ++i)
	{
		const auto id = cast.getNewSessionID();
		ASSERT_TRUE(id);
		EXPECT_GE(*id, cast.first());
		EXPECT_LE(*id, cast.last());
	}
	EXPECT_FALSE(cast.getNewSessionID());
}

TEST(SessionIds, ReleasingAMainServerIdDoesNotFeedTheLocalPool)
{
	SessionIdManager cast{ firstLocalSessionId, 2 };
	const auto first = cast.getNewSessionID();
	const auto second = cast.getNewSessionID();
	ASSERT_TRUE(first && second);
	cast.releaseSessionID(victimSession);
	EXPECT_FALSE(cast.getNewSessionID());
	cast.releaseSessionID(*first);
	EXPECT_EQ(cast.getNewSessionID(), first);
}

TEST(SessionIds, MainServerOrderUnchanged)
{
	SessionIdManager main{ 1, Common::Constants::maxSessionsPerServer };
	EXPECT_EQ(main.getNewSessionID(), std::size_t{ Common::Constants::maxSessionsPerServer });
	EXPECT_EQ(main.getNewSessionID(), std::size_t{ Common::Constants::maxSessionsPerServer - 1u });
}

TEST(CastConnect, IpcLayout)
{
	EXPECT_EQ(sizeof(CastConnectCheck), 12u);
	EXPECT_EQ(sizeof(CastConnectAnswer), 4u);
	EXPECT_EQ(sizeof(ConnectRequest), 12u);
	EXPECT_GE(authenticationDeadline, 2 * verifyTimeout);
}
