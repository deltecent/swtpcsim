// `telnet:` -- the socket line that speaks the Telnet protocol, ON THE REAL NETWORK
// STACK. Everything here is provable over loopback with nothing but a free port: a
// raw client connects, and we assert the exact bytes the negotiation puts on the
// wire and the exact bytes the guest reads back out. The point of the endpoint is
// the handshake a raw socket: cannot do, so the handshake is what the test pins.
//
// Like the socket sections in test_lines.cpp / test_modemline.cpp, this touches the
// kernel TCP stack, so every state change is waited for by WALL CLOCK (waitFor).

#include "host/endpoint.h"
#include "host/stream.h"
#include "platform/socket.h"
#include "test.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace swtpc;

namespace {

// Telnet bytes, named (RFC 854/857/858). Only what the assertions reference.
constexpr uint8_t IAC = 255, DONT = 254, DO = 253, WILL = 251;
constexpr uint8_t OPT_ECHO = 1, OPT_SGA = 3;

// A free port, the OS's way (test_modemline's trick): bind 0, read what it picked,
// drop it. A tiny race remains, which is why the endpoint rebinds it immediately.
uint16_t freePort() {
    std::string err;
    if (auto l = platform::listenTcp(0, err)) return l->port();
    return 0;
}

template <class Step, class Pred>
bool waitFor(Step step, Pred done) {
    for (int i = 0; i < 200 && !done(); ++i) {
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

// Everything currently readable on a raw conn, as a string.
std::string drain(platform::TcpConn* c) {
    std::string s;
    uint8_t     b[512];
    for (;;) {
        size_t r = c->read(b, sizeof b);
        if (r == 0) break;
        s.append((const char*)b, r);
    }
    return s;
}

} // namespace

void test_telnet() {
    // -----------------------------------------------------------------------
    // THE LISTEN (SERVER) ROLE. A human telnets in; on connect the server offers
    // exactly WILL ECHO, WILL SGA, DO SGA -- the trio that makes a stock client drop
    // its local echo and go character-at-a-time. Inbound IAC is stripped, CR LF folds
    // to a bare CR, IAC IAC is one literal 0xFF, and an unknown option is refused.
    // -----------------------------------------------------------------------
    SECTION("telnet: -- a LISTEN offers WILL ECHO/SGA so a client drops local echo");
    {
        std::string err;
        uint16_t    port = freePort();
        CHECK(port != 0, "got a free port to listen on");

        auto stream = resolveEndpoint("telnet:" + std::to_string(port), err);
        CHECK(stream != nullptr, ("telnet:PORT resolves: " + err).c_str());
        CHECK(stream && stream->describe() == "telnet:" + std::to_string(port),
              "describe() round-trips telnet:PORT (SHOW / CONFIG SAVE)");

        auto client = platform::connectTcp("127.0.0.1", port, err);
        CHECK(client != nullptr, ("a client telnets in: " + err).c_str());

        // The negotiation lands the instant the listener accepts.
        std::string neg;
        waitFor([&] { if (client) client->poll();
                      if (stream) stream->pump();
                      if (client) neg += drain(client.get()); },
                [&] { return client && client->established() && neg.size() >= 9; });

        const std::string wantNeg = {(char)IAC, (char)WILL, (char)OPT_ECHO,
                                     (char)IAC, (char)WILL, (char)OPT_SGA,
                                     (char)IAC, (char)DO,   (char)OPT_SGA};
        CHECK(neg == wantNeg, "the server offers WILL ECHO, WILL SGA, DO SGA on connect");

        // The client speaks: an unknown option it WILLs (server must refuse), then
        // data with a CR LF line ending and an escaped 0xFF in the middle.
        const std::vector<uint8_t> in = {IAC, WILL, 24, 'A', 'B', '\r', '\n', IAC, IAC, 'Z'};
        if (client) client->write(in.data(), in.size());

        std::string got, resp;
        waitFor([&] { if (client) client->poll();
                      if (stream) { stream->pump();
                          uint8_t b[64]; size_t r;
                          while ((r = stream->read(b, sizeof b)) > 0) got.append((const char*)b, r); }
                      if (client) resp += drain(client.get()); },
                [&] { return got.size() >= 5 && resp.size() >= 3; });

        const std::string wantData = {'A', 'B', '\r', '\xFF', 'Z'};
        CHECK(got == wantData,
              "inbound IAC stripped, CR LF folds to CR, IAC IAC yields one literal 0xFF");
        const std::string wantResp = {(char)IAC, (char)DONT, 24};
        CHECK(resp == wantResp, "an unknown option the client WILLs is refused with DONT");

        // The guest writes back a byte with a 0xFF in it: it must be DOUBLED on the wire.
        const uint8_t out[] = {'X', 0xFF, 'Y'};
        if (stream) stream->write(out, sizeof out);
        std::string wire;
        waitFor([&] { if (client) client->poll();
                      if (stream) stream->pump();
                      if (client) wire += drain(client.get()); },
                [&] { return wire.size() >= 4; });
        const std::string wantWire = {'X', '\xFF', '\xFF', 'Y'};
        CHECK(wire == wantWire, "a data 0xFF is doubled (IAC IAC) on the way out");
    }

    // -----------------------------------------------------------------------
    // THE DIAL-OUT (CLIENT) ROLE. telnet:HOST:PORT dials a far end and takes the
    // client's part: it asks the SERVER to echo and suppress go-ahead, and never
    // offers to echo itself.
    // -----------------------------------------------------------------------
    SECTION("telnet: -- a dial-OUT asks the far end to echo (DO ECHO / DO SGA)");
    {
        std::string err;
        auto        bbs = platform::listenTcp(0, err);  // the far end we dial
        CHECK(bbs != nullptr, ("a far-end listener to dial: " + err).c_str());

        if (bbs) {
            uint16_t port   = bbs->port();
            auto     stream = resolveEndpoint("telnet:127.0.0.1:" + std::to_string(port), err);
            CHECK(stream != nullptr, ("telnet:HOST:PORT resolves: " + err).c_str());

            std::unique_ptr<platform::TcpConn> server;
            std::string                        neg;
            waitFor([&] { if (stream) stream->pump();
                          if (!server) server = bbs->accept();
                          if (server) { server->poll(); neg += drain(server.get()); } },
                    [&] { return server && neg.size() >= 6; });

            const std::string wantNeg = {(char)IAC, (char)DO, (char)OPT_ECHO,
                                         (char)IAC, (char)DO, (char)OPT_SGA};
            CHECK(neg == wantNeg, "the client asks the server to echo and suppress go-ahead");
        }
    }

    // -----------------------------------------------------------------------
    // The grammar is socket:'s, so its errors are too: a bad port is caught, and the
    // scheme names itself in the message.
    // -----------------------------------------------------------------------
    SECTION("telnet: -- grammar errors name the scheme");
    {
        std::string err;
        CHECK(resolveEndpoint("telnet:", err) == nullptr, "telnet: with no port refuses");
        CHECK(err.find("telnet:") != std::string::npos, "...and the error names telnet:");
        err.clear();
        CHECK(resolveEndpoint("telnet:notaport", err) == nullptr, "telnet: with a bad port refuses");
    }
}
