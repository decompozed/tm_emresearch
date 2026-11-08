// Usage: CastLoadTest --clients 300 --room 16 --tick 10 --seconds 20 --shots 0.2
//        [--hit-requests 1] [--item-requests 0.05] [--pickups 0.05] [--host-hits 2] [--explosions 0.3] [--damage 100000] [--respawn 3]
//        [--cast 127.0.0.1:13006] [--main-ipc 14005] [--process CastServer.exe] [--min-delivery 99]
//        [--ipc-secret-env MV_IPC_SECRET]

#include "asio.hpp"
#include "Network/IpcTls.h"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <sstream>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

using asio::ip::tcp;
using Clock = std::chrono::steady_clock;
namespace IpcTls = Common::Network::IpcTls;

namespace
{
    constexpr std::uint16_t orderCastConnect = 4;
    constexpr std::uint16_t orderPassIp = 6;
    constexpr std::uint16_t orderLinkHello = 19;
    constexpr std::uint16_t orderRoomSync = 20;
    constexpr std::uint16_t orderRoomSyncRequest = 21;
    constexpr std::uint16_t roomMode = 0;
    constexpr std::uint16_t roomMap = 1;
    constexpr int fullHp = 1'000'000;
    constexpr auto lethalHitDelay = std::chrono::milliseconds(500);
    constexpr std::array<std::uint16_t, 11> combatOrders{ 152, 146, 96, 166, 268, 266, 269, 264, 265, 262, 276 };

    std::uint32_t hashKeyOf(std::uint32_t accountId) { return 0x9E3779B9u ^ accountId; }
    std::uint32_t unitOf(std::uint32_t session) { return session | (1u << 16); }

    struct Options
    {
        int clients = 100;
        int roomSize = 16;
        int tick = 10;
        int seconds = 20;
        double shots = 0.2;
        double hitRequests = 1;
        double itemRequests = 0.05;
        double pickups = 0.05;
        double hostHits = 2;
        double explosions = 0.3;
        int damage = 100'000;
        double respawnSeconds = 3;
        double minDelivery = 0;
        double maxP99 = 0;
        std::string castHost = "127.0.0.1";
        std::uint16_t castPort = 13006;
        std::uint16_t mainIpcPort = 14005;
        std::string ipcSecretEnv;
        #ifdef _WIN32
        std::string process = "CastServer.exe";
#else
        std::string process = "CastServer.elf";
#endif
    };

    struct Frame
    {
        std::uint32_t session;
        std::uint16_t order;
        std::uint8_t mission, extra, option;
        std::vector<std::uint8_t> payload;
    };

    std::vector<std::uint8_t> encode(std::uint32_t session, std::uint16_t order, std::uint8_t mission, std::uint8_t extra,
        std::uint8_t option, const void* payload = nullptr, std::size_t size = 0)
    {
        const std::uint32_t total = static_cast<std::uint32_t>(8 + size);
        const std::uint32_t tcp = ((session & 0x3FFF) << 4) | ((total & 0x7FF) << 18);
        const std::uint32_t cmd = ((mission & 3u) << 4) | ((order & 0x3FFu) << 6) | (std::uint32_t{ extra } << 16) |
            (std::uint32_t{ option } << 24);
        std::vector<std::uint8_t> out(total);
        std::memcpy(out.data(), &tcp, 4);
        std::memcpy(out.data() + 4, &cmd, 4);
        if (size) std::memcpy(out.data() + 8, payload, size);
        return out;
    }

    template <class F>
    void drainFrames(std::vector<std::uint8_t>& buffer, F&& onFrame)
    {
        std::size_t pos = 0;
        while (buffer.size() - pos >= 8)
        {
            std::uint32_t tcp, cmd;
            std::memcpy(&tcp, buffer.data() + pos, 4);
            const std::uint32_t total = (tcp >> 18) & 0x7FF;
            if (total < 8) { buffer.clear(); return; }
            if (buffer.size() - pos < total) break;
            std::memcpy(&cmd, buffer.data() + pos + 4, 4);
            Frame f{ (tcp >> 4) & 0x3FFF, static_cast<std::uint16_t>((cmd >> 6) & 0x3FF), static_cast<std::uint8_t>((cmd >> 4) & 3),
                static_cast<std::uint8_t>((cmd >> 16) & 0xFF), static_cast<std::uint8_t>(cmd >> 24),
                std::vector<std::uint8_t>(buffer.begin() + pos + 8, buffer.begin() + pos + total) };
            onFrame(f);
            pos += total;
        }
        buffer.erase(buffer.begin(), buffer.begin() + pos);
    }

    std::uint16_t toHalf(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, 4);
        const std::uint32_t sign = (bits >> 16) & 0x8000;
        const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
        if (exponent <= 0) return static_cast<std::uint16_t>(sign);
        if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7BFF);
        return static_cast<std::uint16_t>(sign | (exponent << 10) | ((bits >> 13) & 0x3FF));
    }

    class Payload
    {
    public:
        template <class T>
            requires std::is_integral_v<T>
        Payload& put(T value)
        {
            const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(value);
            m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
            return *this;
        }

        Payload& half3(float x, float y, float z) { return put(toHalf(x)).put(toHalf(y)).put(toHalf(z)); }
        Payload& trace(float x, float y) { return put(std::uint32_t{ 0 }).half3(x, y, 100).half3(1, 0, 0); }
        const std::vector<std::uint8_t>& bytes() const { return m_bytes; }

    private:
        std::vector<std::uint8_t> m_bytes;
    };

    template <class T>
        requires std::is_integral_v<T>
    T load(const std::vector<std::uint8_t>& payload, std::size_t offset)
    {
        T value{};
        if (offset + sizeof(T) <= payload.size()) std::memcpy(&value, payload.data() + offset, sizeof(T));
        return value;
    }

    std::size_t entrySize(std::uint32_t flags)
    {
        return 4 + ((flags >> 14 & 1) ? 12 : 0) + ((flags >> 15 & 1) ? 8 : 0) + ((flags >> 23 & 1) ? 4 : 0) + ((flags >> 31) ? 4 : 0);
    }

    struct Stats
    {
        std::mutex mutex;
        std::vector<double> rttMs;
        std::atomic<std::uint64_t> entries{ 0 };
        std::atomic<std::uint64_t> batches{ 0 };
        std::atomic<std::uint64_t> sent{ 0 };
        std::atomic<std::uint64_t> errors{ 0 };
        std::atomic<std::uint64_t> kills{ 0 };
        std::array<std::atomic<std::uint64_t>, 1024> sentOrders{};
        std::array<std::atomic<std::uint64_t>, 1024> receivedOrders{};
        std::atomic<bool> measuring{ false };
        std::atomic<bool> combat{ true };
    };

    class FakeMain
    {
    public:
        FakeMain(asio::io_context& io, std::uint16_t port, IpcTls::Context* tls)
            : m_acceptor(io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), port)), m_strand(asio::make_strand(io)),
              m_socket(std::make_unique<tcp::socket>(m_strand)), m_tls(tls)
        {
            m_acceptor.async_accept(*m_socket, asio::bind_executor(m_strand, [this](std::error_code ec) {
                if (ec) return;
                m_socket->set_option(tcp::no_delay(true));
                if (!m_tls)
                {
                    read();
                    return;
                }
                m_link = std::make_unique<IpcTls::Link>(*m_socket, *m_tls, IpcTls::Role::Server, IpcTls::mainIdentity(1));
                m_link->handshake(IpcTls::handshakeTimeout, [this](const std::error_code& error) {
                    if (error) { std::cerr << "[main] TLS handshake failed: " << error.message() << "\n"; return; }
                    read();
                });
            }));
        }

        bool linkUp() const { return m_linkUp; }

        void roomSync(std::uint32_t roomNumber, std::uint16_t host, const std::vector<std::uint16_t>& members)
        {
            Payload data;
            data.put(roomNumber).put(++m_sequence).put(host).put(static_cast<std::uint8_t>(members.size())).put(std::uint8_t{ 0 }).put(roomMode).put(roomMap);
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                data.put(members[i]).put(static_cast<std::uint8_t>(1 + i % 2)).put(std::uint8_t{ 0 });
            }
            asio::post(m_strand, [this, roomNumber, host, members] { m_rooms[roomNumber] = { host, members }; });
            write(encode(0, orderRoomSync, 0, 0, 0, data.bytes().data(), data.bytes().size()));
        }

        void passIp(const std::string& ip)
        {
            std::string data = ip;
            data.push_back('\0');
            write(encode(0, orderPassIp, 0, 0, 0, data.data(), data.size()));
        }

    private:
        template <class Operation>
        void onStream(Operation&& operation)
        {
            if (m_link) operation(m_link->stream());
            else operation(*m_socket);
        }

        void read()
        {
            onStream([this](auto& stream) {
                stream.async_read_some(asio::buffer(m_chunk), asio::bind_executor(m_strand, [this](std::error_code ec, std::size_t n) {
                    if (ec) { std::cerr << "[main] link closed\n"; m_linkUp = false; return; }
                    m_buffer.insert(m_buffer.end(), m_chunk.begin(), m_chunk.begin() + n);
                    drainFrames(m_buffer, [this](const Frame& f) { onFrame(f); });
                    read();
                }));
            });
        }

        void onFrame(const Frame& f)
        {
            if (f.order == orderLinkHello) m_linkUp = true;
            else if (f.order == orderRoomSyncRequest && f.payload.size() == 4)
            {
                std::uint32_t roomNumber;
                std::memcpy(&roomNumber, f.payload.data(), 4);
                if (const auto it = m_rooms.find(roomNumber); it != m_rooms.end())
                {
                    const auto [host, members] = it->second;
                    asio::post(m_strand, [this, roomNumber, host, members] { roomSync(roomNumber, host, members); });
                }
            }
            else if (f.order == orderCastConnect && f.payload.size() == 12)
            {
                std::uint32_t check[3];
                std::memcpy(check, f.payload.data(), 12);
                const bool accepted = check[1] == hashKeyOf(check[0]) && (check[2] & 0x7FFFFFFF) == unitOf(check[0]);
                const std::uint8_t answer[4] = { static_cast<std::uint8_t>(check[0]), static_cast<std::uint8_t>(check[0] >> 8),
                    static_cast<std::uint8_t>(accepted ? 0 : 3), 0 };
                write(encode(0, orderCastConnect, 0, 0, f.option, answer, 4));
            }
        }

        void write(std::vector<std::uint8_t> data)
        {
            asio::post(m_strand, [this, data = std::move(data)]() mutable {
                m_queue.push_back(std::move(data));
                if (m_queue.size() == 1) flush();
            });
        }

        void flush()
        {
            onStream([this](auto& stream) {
                asio::async_write(stream, asio::buffer(m_queue.front()), asio::bind_executor(m_strand, [this](std::error_code ec, std::size_t) {
                    if (ec) return;
                    m_queue.pop_front();
                    if (!m_queue.empty()) flush();
                }));
            });
        }

        tcp::acceptor m_acceptor;
        asio::strand<asio::io_context::executor_type> m_strand;
        std::unique_ptr<tcp::socket> m_socket;
        IpcTls::Context* m_tls;
        std::unique_ptr<IpcTls::Link> m_link;
        std::array<std::uint8_t, 65536> m_chunk{};
        std::vector<std::uint8_t> m_buffer;
        std::deque<std::vector<std::uint8_t>> m_queue;
        std::atomic<bool> m_linkUp{ false };
        std::atomic<std::uint32_t> m_sequence{ 0 };
        std::map<std::uint32_t, std::pair<std::uint16_t, std::vector<std::uint16_t>>> m_rooms;
    };

    class Client : public std::enable_shared_from_this<Client>
    {
    public:
        Client(asio::io_context& io, std::uint32_t sessionId, const Options& options, Stats& stats)
            : m_strand(asio::make_strand(io)), m_socket(m_strand), m_sendTimer(m_strand), m_pingTimer(m_strand),
              m_session(sessionId), m_options(options), m_stats(stats), m_rng(sessionId)
        {
        }

        std::string connect(const tcp::endpoint& endpoint)
        {
            std::error_code ec;
            m_socket.connect(endpoint, ec);
            if (ec) return "tcp connect: " + ec.message();
            if (!waitFor(401)) return "no 401";
            const std::uint32_t request[3] = { unitOf(m_session), m_session, hashKeyOf(m_session) };
            if (!sendBlocking(encode(0, 252, 0, 0, 0, request, sizeof(request)))) return "252 not sent";
            return waitFor(501) ? std::string{} : "no 501 after 252";
        }

        bool sendBlocking(const std::vector<std::uint8_t>& data)
        {
            std::error_code ec;
            asio::write(m_socket, asio::buffer(data), ec);
            return !ec;
        }

        std::uint32_t session() const { return m_session; }

        void joinRoom(const std::vector<std::shared_ptr<Client>>& room)
        {
            m_isHost = room.front().get() == this;
            for (const auto& member : room)
            {
                if (member.get() == this) continue;
                m_others.push_back(member->session());
                if (m_isHost) m_targets.push_back({ member });
            }
        }

        void start(double phase)
        {
            m_startedAt = Clock::now();
            read();
            const auto period = std::chrono::microseconds(1'000'000 / m_options.tick);
            m_next = Clock::now() + std::chrono::duration_cast<Clock::duration>(period * phase);
            scheduleSend();
            m_pingTimer.expires_after(std::chrono::milliseconds(500 + m_rng() % 1000));
            m_pingTimer.async_wait([self = shared_from_this()](std::error_code ec) { if (!ec) self->ping(); });
        }

        void stop()
        {
            asio::post(m_strand, [self = shared_from_this()] {
                std::error_code ec;
                self->m_sendTimer.cancel();
                self->m_pingTimer.cancel();
                self->m_socket.close(ec);
            });
        }

    private:
        bool waitFor(std::uint16_t order)
        {
            const auto deadline = Clock::now() + std::chrono::seconds(10);
            while (Clock::now() < deadline)
            {
                std::error_code ec;
                const std::size_t n = m_socket.read_some(asio::buffer(m_chunk), ec);
                if (ec) return false;
                m_buffer.insert(m_buffer.end(), m_chunk.begin(), m_chunk.begin() + n);
                bool found = false;
                drainFrames(m_buffer, [&](const Frame& f) { found = found || f.order == order; });
                if (found) return true;
            }
            return false;
        }

        void read()
        {
            m_socket.async_read_some(asio::buffer(m_chunk), [self = shared_from_this()](std::error_code ec, std::size_t n) {
                if (ec) { if (self->m_stats.measuring) ++self->m_stats.errors; return; }
                self->m_buffer.insert(self->m_buffer.end(), self->m_chunk.begin(), self->m_chunk.begin() + n);
                drainFrames(self->m_buffer, [&](const Frame& f) { self->onFrame(f); });
                self->read();
            });
        }

        void onFrame(const Frame& f)
        {
            if (m_stats.measuring) ++m_stats.receivedOrders[f.order];
            if (f.order == 276 && f.payload.size() == 12 && load<std::uint32_t>(f.payload, 8) == unitOf(m_session)) ++m_lives;
            if (m_isHost && (f.order == 152 || f.order == 146 || f.order == 96 || f.order == 166)) onRequest(f);
            if (f.order != 322 || f.payload.size() < 4) return;
            ++m_stats.batches;
            std::size_t pos = 4;
            const auto now = Clock::now();
            for (int i = 0; i < f.option && pos + 4 <= f.payload.size(); ++i)
            {
                std::uint32_t flags;
                std::memcpy(&flags, f.payload.data() + pos, 4);
                const std::size_t size = entrySize(flags);
                if (pos + size > f.payload.size()) break;
                ++m_stats.entries;
                if ((flags & 0x3FFF) == m_session && (flags >> 14 & 1))
                {
                    std::uint16_t dirX, dirY;
                    std::memcpy(&dirX, f.payload.data() + pos + 10, 2);
                    std::memcpy(&dirY, f.payload.data() + pos + 12, 2);
                    const std::uint32_t seq = ((dirY & 0x3FFu) << 10) | (dirX & 0x3FFu);
                    if (const auto it = m_inFlight.find(seq); it != m_inFlight.end())
                    {
                        if (m_stats.measuring)
                        {
                            const double ms = std::chrono::duration<double, std::milli>(now - it->second).count();
                            std::lock_guard lock(m_stats.mutex);
                            m_stats.rttMs.push_back(ms);
                        }
                        m_inFlight.erase(m_inFlight.begin(), std::next(it));
                    }
                }
                pos += size;
            }
        }

        void scheduleSend()
        {
            m_next += std::chrono::microseconds(1'000'000 / m_options.tick);
            m_sendTimer.expires_at(m_next);
            m_sendTimer.async_wait([self = shared_from_this()](std::error_code ec) { if (!ec) self->sendPosition(); });
        }

        void sendPosition()
        {
            m_x += static_cast<float>(m_rng() % 200) / 100.0f - 1.0f;
            m_y += static_cast<float>(m_rng() % 200) / 100.0f - 1.0f;
            const std::uint32_t seq = ++m_seq & 0xFFFFF;
            std::uint8_t payload[28]{};
            const std::uint16_t pos[3] = { toHalf(500 + m_x), toHalf(500 + m_y), toHalf(100) };
            const std::uint16_t dir[3] = { static_cast<std::uint16_t>(0x3C00 | (seq & 0x3FF)),
                static_cast<std::uint16_t>(0x3C00 | ((seq >> 10) & 0x3FF)), 0x3C00 };
            const bool shot = std::uniform_real_distribution<double>(0, 1)(m_rng) < m_options.shots;
            const std::uint32_t matchTick = static_cast<std::uint32_t>(m_seq * (100 / m_options.tick)) | (shot ? 0x80000000u : 0u);
            const std::uint32_t bits = 1u | (12u << 7) | (1u << 13);
            std::memcpy(payload, pos, 6);
            std::memcpy(payload + 6, dir, 6);
            std::memcpy(payload + 12, &matchTick, 4);
            std::memcpy(payload + 16, &bits, 4);
            if (shot)
            {
                const std::uint16_t bullet[4] = { toHalf(510), toHalf(510), toHalf(100), 1 };
                std::memcpy(payload + 20, bullet, 8);
            }
            m_inFlight[seq] = Clock::now();
            if (m_inFlight.size() > 4096) m_inFlight.erase(m_inFlight.begin());
            write(encode(m_session, 281, 0, 0, 0, payload, shot ? 28 : 20));
            if (m_stats.combat)
            {
                if (m_isHost) hostTick();
                else memberTick();
            }
            scheduleSend();
        }

        struct Target
        {
            std::shared_ptr<Client> client;
            int hp = 0;
            bool alive = false;
            std::optional<Clock::time_point> lethalAt;
        };

        int events(double perSecond) { return perSecond > 0 ? std::poisson_distribution<int>(perSecond / m_options.tick)(m_rng) : 0; }
        float coordinate() { return 480 + static_cast<float>(m_rng() % 4000) / 100.0f; }
        std::uint32_t matchTime() const
        {
            return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_startedAt).count() / 10);
        }

        void send(std::uint16_t order, std::uint8_t mission, std::uint8_t extra, std::uint8_t option, const Payload& payload = {})
        {
            write(encode(m_session, order, mission, extra, option, payload.bytes().data(), payload.bytes().size()));
        }

        std::uint32_t randomOther() { return m_others[m_rng() % m_others.size()]; }

        void memberTick()
        {
            if (m_respawnAt && Clock::now() >= *m_respawnAt)
            {
                m_respawnAt.reset();
                send(166, 0, 0, 0);
            }
            if (m_lives <= m_deaths || m_others.empty()) return;
            for (int i = events(m_options.hitRequests); i > 0; --i)
            {
                send(152, 0, 1, 0, Payload{}.trace(coordinate(), coordinate()).put(unitOf(randomOther())).half3(coordinate(), coordinate(), 100)
                    .put(static_cast<std::uint16_t>(m_rng() % 6)));
            }
            for (int i = events(m_options.itemRequests); i > 0; --i)
            {
                send(146, 0, 0, 0, Payload{}.put(unitOf(randomOther())).put(static_cast<std::uint32_t>(1 + m_rng() % 200)));
            }
            for (int i = events(m_options.pickups); i > 0; --i)
            {
                send(96, 0, 0, 0, Payload{}.put((m_session << 16) | (++m_nextObject & 0xFFFF)).put(std::uint32_t{ 0 }));
            }
        }

        void hostTick()
        {
            if (!m_spawned)
            {
                m_spawned = true;
                respawn(m_session);
                for (auto& target : m_targets) respawn(target);
            }
            const auto now = Clock::now();
            for (auto& target : m_targets)
            {
                if (target.lethalAt && now >= *target.lethalAt) kill(target);
            }
            for (int i = events(m_options.hostHits); i > 0; --i) hostShot();
            for (int i = events(m_options.explosions); i > 0; --i) explosion();
        }

        void respawn(std::uint32_t session)
        {
            send(276, 0, 0, 0, Payload{}.half3(coordinate(), coordinate(), 100).put(std::uint16_t{ 0 }).put(unitOf(session)));
        }

        void respawn(Target& target)
        {
            respawn(target.client->session());
            target.hp = fullHp;
            target.alive = true;
        }

        std::optional<std::uint32_t> damage(Target& target)
        {
            if (!target.alive || target.lethalAt) return std::nullopt;
            target.hp -= std::uniform_int_distribution<int>(m_options.damage / 2, m_options.damage * 3 / 2)(m_rng);
            if (target.hp <= 0)
            {
                target.hp = 1;
                target.lethalAt = Clock::now() + lethalHitDelay;
                asio::post(target.client->m_strand, [client = target.client] { ++client->m_deaths; });
            }
            return static_cast<std::uint32_t>(target.hp) | (static_cast<std::uint32_t>(m_rng() % 6) << 20);
        }

        void kill(Target& target)
        {
            target.lethalAt.reset();
            target.alive = false;
            send(268, 0, 1, 0, Payload{}.trace(coordinate(), coordinate()).put(unitOf(m_session)).put(unitOf(target.client->session()))
                .put(std::uint32_t{ 0 }).put(matchTime()));
            if (m_stats.measuring) ++m_stats.kills;
            const auto respawnAt = Clock::now() + std::chrono::milliseconds(static_cast<int>(m_options.respawnSeconds * 1000));
            asio::post(target.client->m_strand, [client = target.client, respawnAt] { client->m_respawnAt = respawnAt; });
        }

        Target* findTarget(std::uint32_t session)
        {
            const auto it = std::ranges::find_if(m_targets, [&](const Target& target) { return target.client->session() == session; });
            return it == m_targets.end() ? nullptr : &*it;
        }

        std::vector<Target*> randomTargets(std::size_t count)
        {
            std::vector<Target*> alive;
            for (auto& target : m_targets)
            {
                if (target.alive && !target.lethalAt) alive.push_back(&target);
            }
            std::shuffle(alive.begin(), alive.end(), m_rng);
            if (alive.size() > count) alive.erase(alive.begin() + static_cast<std::ptrdiff_t>(count), alive.end());
            return alive;
        }

        void singleHit(std::uint32_t attacker, Target& target)
        {
            if (const auto hp = damage(target))
            {
                send(268, 0, 1, 0, Payload{}.trace(coordinate(), coordinate()).put(unitOf(attacker)).put(unitOf(target.client->session())).put(*hp)
                    .put(matchTime()));
            }
        }

        std::uint8_t damageEntries(Payload& payload, const std::vector<Target*>& targets)
        {
            std::uint8_t count = 0;
            for (Target* target : targets)
            {
                if (const auto hp = damage(*target))
                {
                    payload.put(unitOf(target->client->session())).put(*hp).put(matchTime());
                    ++count;
                }
            }
            return count;
        }

        void hostShot()
        {
            const auto kind = m_rng() % 3;
            const auto targets = randomTargets(kind == 0 ? 1 : 1 + m_rng() % 3);
            if (targets.empty()) return;
            if (kind == 0)
            {
                singleHit(m_session, *targets.front());
                return;
            }
            Payload payload;
            payload.trace(coordinate(), coordinate()).put(unitOf(m_session));
            if (const auto count = damageEntries(payload, targets); count > 0) send(kind == 1 ? 266 : 269, 0, 1, count, payload);
        }

        void explosion()
        {
            const auto targets = randomTargets(1 + m_rng() % 4);
            if (targets.empty()) return;
            const std::uint32_t source = m_rng() % 2 ? m_session : randomOther();
            Payload payload;
            payload.put(unitOf(source)).half3(coordinate(), coordinate(), 100).put(static_cast<std::uint16_t>(m_rng() % 64));
            if (const auto count = damageEntries(payload, targets); count > 0) send(264, 0, 2, count, payload);
        }

        void onRequest(const Frame& f)
        {
            Target* requester = findTarget(f.session);
            if (!requester) return;
            if (f.order == 166 && !requester->alive)
            {
                respawn(*requester);
                return;
            }
            if (!requester->alive) return;
            if (f.order == 152 && f.payload.size() == 28)
            {
                if (Target* target = findTarget(load<std::uint32_t>(f.payload, 16) & 0xFFFF)) singleHit(f.session, *target);
            }
            else if (f.order == 146 && f.payload.size() == 8)
            {
                Target* target = findTarget(load<std::uint32_t>(f.payload, 0) & 0xFFFF);
                if (target && target->alive)
                {
                    send(265, 0, 0, 0, Payload{}.put(unitOf(target->client->session())).put(load<std::uint32_t>(f.payload, 4)).put(unitOf(f.session)));
                }
            }
            else if (f.order == 96 && f.payload.size() == 8)
            {
                const auto object = load<std::uint32_t>(f.payload, 0);
                send(262, 0, 0, 0, Payload{}.put(object).put(1 + object % 300).put(unitOf(f.session)));
            }
        }

        void ping()
        {
            write(encode(m_session, 71, 1, 0, 0));
            m_pingTimer.expires_after(std::chrono::seconds(1));
            m_pingTimer.async_wait([self = shared_from_this()](std::error_code ec) { if (!ec) self->ping(); });
        }

        void write(std::vector<std::uint8_t> data)
        {
            if (m_stats.measuring)
            {
                ++m_stats.sent;
                ++m_stats.sentOrders[(load<std::uint32_t>(data, 4) >> 6) & 0x3FF];
            }
            m_queue.push_back(std::move(data));
            if (m_queue.size() == 1) flush();
        }

        void flush()
        {
            asio::async_write(m_socket, asio::buffer(m_queue.front()), [self = shared_from_this()](std::error_code ec, std::size_t) {
                if (ec) return;
                self->m_queue.pop_front();
                if (!self->m_queue.empty()) self->flush();
            });
        }

        asio::strand<asio::io_context::executor_type> m_strand;
        tcp::socket m_socket;
        asio::steady_timer m_sendTimer, m_pingTimer;
        std::uint32_t m_session;
        const Options& m_options;
        Stats& m_stats;
        std::mt19937 m_rng;
        std::array<std::uint8_t, 65536> m_chunk{};
        std::vector<std::uint8_t> m_buffer;
        std::deque<std::vector<std::uint8_t>> m_queue;
        std::map<std::uint32_t, Clock::time_point> m_inFlight;
        Clock::time_point m_next;
        std::uint32_t m_seq = 0;
        float m_x = 0, m_y = 0;
        Clock::time_point m_startedAt;
        bool m_isHost = false;
        bool m_spawned = false;
        std::vector<std::uint32_t> m_others;
        std::vector<Target> m_targets;
        std::uint32_t m_lives = 0;
        std::uint32_t m_deaths = 0;
        std::optional<Clock::time_point> m_respawnAt;
        std::uint32_t m_nextObject = 0;
    };

#ifdef _WIN32
    struct ProcessCpu
    {
        HANDLE handle = nullptr;

        explicit ProcessCpu(const std::string& name)
        {
            std::wstring wide(name.begin(), name.end());
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            PROCESSENTRY32W entry{ sizeof(entry) };
            DWORD pid = 0;
            for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
            {
                if (_wcsicmp(entry.szExeFile, wide.c_str()) == 0) pid = entry.th32ProcessID;
            }
            CloseHandle(snapshot);
            if (pid) handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        }

        double seconds() const
        {
            FILETIME created, exited, kernel, user;
            if (!handle || !GetProcessTimes(handle, &created, &exited, &kernel, &user)) return -1;
            auto toSeconds = [](const FILETIME& t) { return ((static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 1e7; };
            return toSeconds(kernel) + toSeconds(user);
        }
    };
#else
    struct ProcessCpu
    {
        std::string statPath;

        explicit ProcessCpu(const std::string& name)
        {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator("/proc", ec))
            {
                std::ifstream comm(entry.path() / "comm");
                std::string value;
                if (comm && std::getline(comm, value) && !value.empty() && name.rfind(value, 0) == 0)
                {
                    statPath = (entry.path() / "stat").string();
                    break;
                }
            }
        }

        double seconds() const
        {
            std::ifstream stat(statPath);
            std::string content((std::istreambuf_iterator<char>(stat)), std::istreambuf_iterator<char>());
            const auto close = content.rfind(')');
            if (close == std::string::npos) return -1;
            std::istringstream fields(content.substr(close + 2));
            std::string field;
            double utime = 0, stime = 0;
            for (int i = 3; fields >> field; ++i)
            {
                if (i == 14) utime = std::stod(field);
                if (i == 15) { stime = std::stod(field); break; }
            }
            return (utime + stime) / static_cast<double>(sysconf(_SC_CLK_TCK));
        }
    };
#endif

    std::optional<std::vector<std::uint8_t>> secretFromEnvironment(const std::string& name)
    {
        const char* value = std::getenv(name.c_str());
        const std::string hex = value ? value : "";
        if (hex.size() != IpcTls::secretBytes * 2) return std::nullopt;
        std::vector<std::uint8_t> secret;
        for (std::size_t i = 0; i < hex.size(); i += 2)
        {
            const auto byte = hex.substr(i, 2);
            if (!std::isxdigit(static_cast<unsigned char>(byte[0])) || !std::isxdigit(static_cast<unsigned char>(byte[1]))) return std::nullopt;
            secret.push_back(static_cast<std::uint8_t>(std::stoul(byte, nullptr, 16)));
        }
        return secret;
    }

    double percentile(std::vector<double>& values, double p)
    {
        if (values.empty()) return 0;
        const std::size_t index = std::min(values.size() - 1, static_cast<std::size_t>(p * (values.size() - 1)));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
        return values[index];
    }
}

int main(int argc, char** argv)
{
    Options options;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--clients") options.clients = std::stoi(value);
        else if (key == "--room") options.roomSize = std::stoi(value);
        else if (key == "--tick") options.tick = std::stoi(value);
        else if (key == "--seconds") options.seconds = std::stoi(value);
        else if (key == "--shots") options.shots = std::stod(value);
        else if (key == "--hit-requests") options.hitRequests = std::stod(value);
        else if (key == "--item-requests") options.itemRequests = std::stod(value);
        else if (key == "--pickups") options.pickups = std::stod(value);
        else if (key == "--host-hits") options.hostHits = std::stod(value);
        else if (key == "--explosions") options.explosions = std::stod(value);
        else if (key == "--damage") options.damage = std::max(2, std::stoi(value));
        else if (key == "--respawn") options.respawnSeconds = std::stod(value);
        else if (key == "--main-ipc") options.mainIpcPort = static_cast<std::uint16_t>(std::stoi(value));
        else if (key == "--process") options.process = value;
        else if (key == "--ipc-secret-env") options.ipcSecretEnv = value;
        else if (key == "--min-delivery") options.minDelivery = std::stod(value);
        else if (key == "--max-p99") options.maxP99 = std::stod(value);
        else if (key == "--cast")
        {
            const auto colon = value.find(':');
            options.castHost = value.substr(0, colon);
            options.castPort = static_cast<std::uint16_t>(std::stoi(value.substr(colon + 1)));
        }
    }

    std::unique_ptr<IpcTls::Context> tls;
    if (!options.ipcSecretEnv.empty())
    {
        const auto secret = secretFromEnvironment(options.ipcSecretEnv);
        if (!secret)
        {
            std::cerr << options.ipcSecretEnv << " must hold 64 hex digits" << std::endl;
            return 2;
        }
        tls = std::make_unique<IpcTls::Context>(*secret);
    }

    asio::io_context io;
    auto guard = asio::make_work_guard(io);
    Stats stats;
    FakeMain fakeMain(io, options.mainIpcPort, tls.get());
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < std::max(2u, std::thread::hardware_concurrency() / 2); ++i) threads.emplace_back([&io] { io.run(); });

    std::cout << "waiting for the CastServer IPC link on port " << options.mainIpcPort << "..." << std::endl;
    const auto linkDeadline = Clock::now() + std::chrono::seconds(60);
    while (!fakeMain.linkUp())
    {
        if (Clock::now() > linkDeadline)
        {
            std::cerr << "no CastServer IPC link" << std::endl;
            guard.reset();
            io.stop();
            for (auto& t : threads) t.join();
            return 2;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const tcp::endpoint castEndpoint(asio::ip::make_address(options.castHost), options.castPort);
    std::vector<std::shared_ptr<Client>> clients;
    for (int i = 0; i < options.clients; ++i)
    {
        auto client = std::make_shared<Client>(io, static_cast<std::uint32_t>(i + 1), options, stats);
        bool connected = false;
        for (int attempt = 0; attempt < 8 && !connected; ++attempt)
        {
            fakeMain.passIp(options.castHost);
            std::this_thread::sleep_for(std::chrono::milliseconds(5 << attempt));
            const std::string failure = client->connect(castEndpoint);
            connected = failure.empty();
            if (!connected) std::cerr << "client " << i + 1 << " attempt " << attempt + 1 << ": " << failure << std::endl;
            if (!connected) client = std::make_shared<Client>(io, static_cast<std::uint32_t>(i + 1), options, stats);
        }
        if (!connected) { std::cerr << "client " << i + 1 << " could not connect, stopping at " << i << " clients\n"; break; }
        clients.push_back(client);
    }

    for (std::size_t first = 0; first < clients.size(); first += options.roomSize)
    {
        const std::vector<std::shared_ptr<Client>> room(clients.begin() + static_cast<std::ptrdiff_t>(first),
            clients.begin() + static_cast<std::ptrdiff_t>(std::min(clients.size(), first + options.roomSize)));
        std::vector<std::uint16_t> members;
        for (const auto& client : room)
        {
            members.push_back(static_cast<std::uint16_t>(client->session()));
            client->joinRoom(room);
        }
        fakeMain.roomSync(static_cast<std::uint32_t>(first / options.roomSize + 1), members.front(), members);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (std::size_t i = 0; i < clients.size(); ++i)
    {
        const auto& host = clients[i - i % options.roomSize];
        if (i % options.roomSize == 0) clients[i]->sendBlocking(encode(clients[i]->session(), 277, 0, 0, 0));
        else clients[i]->sendBlocking(encode(host->session(), 140, 0, 0, 0));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (auto& client : clients) client->sendBlocking(encode(client->session(), 257, 0, 0, 9));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::mt19937 rng(7);
    for (auto& client : clients) client->start(std::uniform_real_distribution<double>(0, 1)(rng));

    std::this_thread::sleep_for(std::chrono::seconds(3));
    const ProcessCpu process(options.process);
    const double cpu0 = process.seconds();
    const auto t0 = Clock::now();
    stats.measuring = true;
    stats.entries = 0;
    stats.batches = 0;
    std::this_thread::sleep_for(std::chrono::seconds(options.seconds));
    stats.measuring = false;
    const double wall = std::chrono::duration<double>(Clock::now() - t0).count();
    const double cpu1 = process.seconds();
    const double cpu = cpu0 < 0 || cpu1 < 0 ? -1 : (cpu1 - cpu0) / wall * 100.0;
    stats.combat = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    for (auto& client : clients) client->stop();
    guard.reset();
    io.stop();
    for (auto& t : threads) t.join();

    std::vector<double> rtt;
    {
        std::lock_guard lock(stats.mutex);
        rtt = stats.rttMs;
    }
    const std::size_t n = clients.size();
    double expected = 0;
    for (std::size_t first = 0; first < n; first += options.roomSize)
    {
        const double k = static_cast<double>(std::min<std::size_t>(options.roomSize, n - first));
        expected += k * k * options.tick;
    }
    const double expectedOwn = static_cast<double>(n) * options.tick * wall;
    const double delivery = 100.0 * static_cast<double>(rtt.size()) / expectedOwn;
    const double p99 = percentile(rtt, 0.99);
    std::printf("ipc %s | clients %zu  room %d  tick %d  | cast CPU %.1f%% of one core | entries/s %.0f (expected %.0f) | batches/s %.0f | "
                "own updates seen %.1f%% | RTT ms p50 %.1f p90 %.1f p99 %.1f max %.1f | errors %llu\n",
        tls ? "tls" : "plain", n, options.roomSize, options.tick, cpu, stats.entries / wall, expected, stats.batches / wall,
        delivery, percentile(rtt, 0.5), percentile(rtt, 0.9), p99,
        rtt.empty() ? 0.0 : *std::max_element(rtt.begin(), rtt.end()), static_cast<unsigned long long>(stats.errors.load()));
    std::printf("orders sent/received per s:");
    for (const auto order : combatOrders)
    {
        std::printf(" %u %.0f/%.0f", static_cast<unsigned>(order), static_cast<double>(stats.sentOrders[order]) / wall, static_cast<double>(stats.receivedOrders[order]) / wall);
    }
    std::printf(" | kills/s %.1f\n", static_cast<double>(stats.kills) / wall);
    const bool failed = n < static_cast<std::size_t>(options.clients) || stats.errors > 0 || delivery < options.minDelivery ||
        (options.maxP99 > 0 && p99 > options.maxP99);
    if (failed)
    {
        std::printf("FAILED (clients connected %zu/%d, min delivery %.1f%%, max p99 %.1f ms)\n", n, options.clients,
            options.minDelivery, options.maxP99);
    }
    return failed ? 1 : 0;
}
