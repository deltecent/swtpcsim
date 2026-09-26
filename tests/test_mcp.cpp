#include "test.h"

#include "cli/monitor.h"
#include "core/board.h"
#include "core/machine.h"
#include "core/machines.h"
#include "host/console.h"
#include "host/filter.h"
#include "host/mirror_stream.h"
#include "host/stream.h"
#include "mcp/server.h"
#include "platform/socket.h"
#include "util/json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <filesystem>
#include <fstream>
#include <istream>
#include <map>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

using namespace swtpc;

// The MCP server is driven by feeding JSON-RPC lines to runMcp and reading the
// replies back -- the same door an assistant uses, over a pair of stringstreams
// instead of a pipe. No mocks: a real built-in machine, a real 6800, a real 6850.
namespace {

// A tiny thread-safe "pipe": feed() appends bytes from any thread (real time, real
// gaps), close() marks EOF once everything fed has been drained. A static
// std::istringstream makes every line available to runMcp's reader thread at once,
// which is fine for an ordinary script but wrong for testing #488's
// notifications/cancelled -- the reader would race straight through a cancellation
// line before the targeted `run` even starts executing, since nothing paces it
// against real time the way a live client's stdin does. FeedBuf lets a test control
// exactly when each line becomes visible, the same way a real client's writes do.
class FeedBuf : public std::streambuf {
public:
    void feed(const std::string& s) {
        std::lock_guard<std::mutex> lk(mu_);
        for (char c : s) q_.push_back(c);
        cv_.notify_all();
    }
    void close() {
        std::lock_guard<std::mutex> lk(mu_);
        closed_ = true;
        cv_.notify_all();
    }

protected:
    int_type underflow() override {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return traits_type::eof();
        ch_ = q_.front();
        q_.pop_front();
        setg(&ch_, &ch_, &ch_ + 1);
        return traits_type::to_int_type(ch_);
    }

private:
    std::mutex              mu_;
    std::condition_variable cv_;
    std::deque<char>        q_;
    bool                    closed_ = false;
    char                    ch_ = 0;
};

// The other half of FeedBuf: an output buffer a second thread can read WHILE the
// server is still writing to it. A plain std::ostringstream cannot be -- reading
// out.str() while runMcp writes is a data race -- so a test that needs to know a
// reply has landed before it does the next thing has to have this. That is the only
// honest way to time a stop request: wait until the setup calls have actually ANSWERED,
// rather than guessing how long a guest boot takes on someone else's runner.
class SinkBuf : public std::streambuf {
public:
    std::string text() const {
        std::lock_guard<std::mutex> lk(mu_);
        return s_;
    }

protected:
    int_type overflow(int_type c) override {
        if (c != traits_type::eof()) {
            std::lock_guard<std::mutex> lk(mu_);
            s_.push_back(traits_type::to_char_type(c));
        }
        return c;
    }
    std::streamsize xsputn(const char* p, std::streamsize n) override {
        std::lock_guard<std::mutex> lk(mu_);
        s_.append(p, (size_t)n);
        return n;
    }

private:
    mutable std::mutex mu_;
    std::string        s_;
};

// The replies in a captured stream, by id -- runScript's tail end, for the tests that
// drive runMcp themselves instead of handing it a finished script.
std::map<int, Json> repliesById(const std::string& text) {
    std::map<int, Json> byId;
    std::istringstream  lines(text);
    std::string         line;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        Json        j;
        std::string err;
        if (Json::parse(line, j, err)) byId[(int)j.at("id").integer()] = j;
    }
    return byId;
}

std::map<int, Json> runScript(Machine& m, const std::string& script,
                              const std::string& mirror = "") {
    std::istringstream in(script);
    std::ostringstream out;
    runMcp(m, in, out, mirror);

    return repliesById(out.str());
}

// A free TCP port the OS confirms unused -- bind port 0, read what it picked, drop it.
uint16_t freePort() {
    std::string err;
    if (auto probe = platform::listenTcp(0, err)) return probe->port();
    return 0;
}

// Poll `ready` for up to ~2 s of REAL time -- the loopback handshake and byte delivery
// are the kernel's to schedule (test_lines' lesson; sockettest's).
template <typename Fn>
bool waitFor(Fn ready, int ms = 2000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

// The MirrorStream now wrapping the console unit (and its inner ScriptedStream), if any.
// After runMcp with --mirror the console's line is Filter(Mirror(scripted)) -- the console
// transform filter is outermost (always), the mirror rides under it, the assistant drives
// the inner scripted. Peel the filter to reach the mirror.
MirrorStream* consoleMirror(Machine& m, ScriptedStream** innerOut = nullptr) {
    for (const auto& b : m.boards())
        for (const auto& u : b->units()) {
            if (u.kind != UnitKind::Serial) continue;
            ByteStream* s = b->unitStream(u.name);
            if (auto* f = dynamic_cast<FilterStream*>(s)) s = f->inner();  // peel the console filter
            if (auto* mir = dynamic_cast<MirrorStream*>(s)) {
                if (innerOut) *innerOut = dynamic_cast<ScriptedStream*>(mir->inner());
                return mir;
            }
        }
    return nullptr;
}

// Load the altair680 built-in -- a whole machine (6800, 680io console 6850, memory + the
// MON680 PROM at FF00), no disk fixture -- the way every section here does. False and a
// CHECK if it is not compiled in.
bool loadAltair680(Machine& m) {
    const BuiltinMachine* mach = nullptr;
    for (const auto& b : builtinMachines())
        if (std::string(b.name) == "altair680") mach = &b;
    CHECK(mach != nullptr, "the altair680 built-in is compiled in");
    if (!mach) return false;
    std::string err;
    CHECK(loadMachine(*mach, m, err), "altair680 loads");
    return true;
}

// Wait until the worker either IS or IS NOT dispatched on a request, by asking the one
// tool that answers out of band: `status`, handled on the reader thread (#490).
//
// THIS IS WHAT REPLACES "sleep 200ms and assume the call under test is under way." A ^C or
// a `notifications/cancelled` is only honoured once the worker has dequeued the call it
// targets: runMcp calls Debugger::clearStopRequest() as it publishes each request id, so a
// signal landing a moment too early is wiped by the very dispatch that was about to run it,
// and the call goes on to serve its whole budget and answer `timeout`. A fixed sleep is a
// guess that the dequeue already happened -- the guess this file warns against two comments
// down, made anyway because there is no reply to wait for. On a loaded Windows CI runner it
// lost: PR #506's first leg, trial 4 of 6.
//
// `in_flight` is set under the same lock as clearStopRequest() and on the very next line, so
// observing it TRUE is proof the clear is already behind us -- a signal raised from here on
// is seen by the `Debugger::stopRequested()` check at the top of the run loop and cannot be
// swallowed. Waiting for FALSE first is what makes the TRUE mean anything: it pins the
// worker as idle before the call under test is fed, so the TRUE that follows can only be
// that call and not the tail of the setup.
//
// Probing cannot perturb what it measures. `status` is answered by the reader without ever
// entering `pending`, so it neither queues nor reorders; it deliberately never becomes
// `currentId`, so it cannot swallow a cancel meant for another id. Probe ids come from
// their own high range, well clear of the sequential ids each test indexes its replies by.
int g_probeId = 8000;

bool waitForWorker(FeedBuf& feedBuf, SinkBuf& sinkBuf, bool wantBusy, int ms = 30000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        const int          pid = ++g_probeId;
        std::ostringstream line;
        line << R"({"jsonrpc":"2.0","id":)" << pid
             << R"(,"method":"tools/call","params":{"name":"status","arguments":{}}})"
             << "\n";
        feedBuf.feed(line.str());

        std::map<int, Json> rep;
        if (waitFor([&] { rep = repliesById(sinkBuf.text()); return rep.count(pid) != 0; },
                    5000) &&
            rep[pid].at("result").at("structuredContent").at("in_flight").boolean() == wantBusy)
            return true;
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

// Drive a LIVE runMcp and interrupt the one call that is meant to be interrupted.
//
// The hard part is not the signal, it is knowing when to send it. A ^C that lands before
// the call under test has been dispatched is CORRECTLY discarded -- the server clears a
// stale flag as it publishes each request id (mcp/server.cpp) -- so a test that guesses
// "the setup is surely done by now" and sleeps is measuring the runner's mood, not the
// server: the guest boot in `setup` takes as long as it takes, and on a loaded Windows
// runner that was longer than the guess. So: feed `setup`, WAIT for its last reply to
// land, and only then feed the call under test. The dequeue of that call was left to a
// 200ms sleep on the grounds that one line read and a mutex is microseconds -- true, and
// still not a guarantee: a loaded Windows runner lost that race (PR #506). waitForWorker()
// above replaces it with the answer from the server itself.
//
// `after` is fed once the stop request has been answered (the stale-flag checks). The raise
// happens on this thread, between two feeds, so it can never escape runMcp's SigintGuard
// -- outside it SIGINT is back to its default disposition and would kill the test binary.
struct SigintRun {
    std::map<int, Json> replies;
    long long           elapsedMs = 0;  // feeding the call under test -> its reply
};

SigintRun runWithSigint(Machine& m, const std::vector<std::string>& setup,
                        const std::string& underTest, const std::vector<std::string>& after) {
    FeedBuf      feedBuf;
    std::istream in(&feedBuf);
    SinkBuf      sinkBuf;
    std::ostream out(&sinkBuf);

    std::thread worker([&] { runMcp(m, in, out, ""); });

    int  id   = 0;
    auto feed = [&](const std::string& params) {
        std::ostringstream line;
        line << R"({"jsonrpc":"2.0","id":)" << ++id
             << R"(,"method":"tools/call","params":)" << params << "}\n";
        feedBuf.feed(line.str());
        return id;
    };
    auto answered = [&](int want) {
        return waitFor([&] { return repliesById(sinkBuf.text()).count(want) != 0; }, 30000);
    };

    for (const auto& call : setup) feed(call);
    CHECK(answered(id), "the setup calls answered before the call under test was sent");
    CHECK(waitForWorker(feedBuf, sinkBuf, false),
          "the worker is idle again before the call under test is fed");

    const int runId = feed(underTest);
    CHECK(waitForWorker(feedBuf, sinkBuf, true),
          "the call under test is DISPATCHED -- so clearStopRequest() is behind us -- before "
          "the signal is raised");
    // Start the clock at DISPATCH, not at the feed: `timeout_ms` is counted from the moment
    // the worker enters the call, so that is the window "well inside the budget" is about.
    // Timing from the feed would charge the queue wait to the stop request.
    const auto t0 = std::chrono::steady_clock::now();
    std::raise(SIGINT);
    CHECK(answered(runId), "the interrupted run answered");
    SigintRun r;
    r.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();

    for (const auto& call : after) feed(call);
    feedBuf.close();
    worker.join();
    r.replies = repliesById(sinkBuf.text());
    return r;
}

std::string tmpPath(const char* leaf) {
    return (std::filesystem::temp_directory_path() / leaf).string();
}

// MON680 has NO banner: on reset it prints CR/LF and a `.` and waits, so a boot run is
// `run {from: 65496, until: "."}` -- 65496 = 0xFFD8, the monitor's RESET entry, not a
// banner, because MCP does not run the machine file's startup. (An explicit `from` sets
// the PC outright: it wins over the 6800's deferred reset-vector fetch -- setPc cancels
// it -- so the entry address must be correct here, not merely close.)
//
// MON680's own examine/deposit command, driven through itself: `M`, a four-hex-digit
// address, then the two hex digits to deposit. `MFF0100` examines FF01 -- the 0x22 operand
// of the `BSR FF24` at FF00 -- so the monitor echoes the address and prints back " FF01 22 "
// before storing 00. That printed `22` is a ROM byte the guest read and typed out itself:
// the 6800 analogue of ALTMON's DUMP, proving send + run + recv move real bytes both ways.
constexpr const char* kMon680Examine = "MFF0100";
constexpr const char* kMon680ExamineEcho = "FF01 22";

// A board that drops a ^C into one exact spot: the gap between the --mcp run loop's
// "was a stop requested?" check and the slice it then runs. That loop reads the backplane's
// rxBytes() between the two, so this board raises the stop request from there. It is how a
// test reaches a window a few instructions wide WITHOUT timing: on Windows the real one
// was hit about once in 550 cancels, which no test can wait for.
//
// ONE-SHOT, and that is load-bearing. The loop also reads rxBytes() AFTER each slice, and
// a stop request raised there is caught by the next top-of-loop check whatever the slice
// does -- raise on every call and the test passes with or without the fix. Armed while the
// worker is idle, the first call after arming is the pre-slice one.
class GapBoard : public Board {
public:
    mutable std::atomic<bool> armed{false};
    std::string type() const override { return "test-gap"; }
    bool decodes(const BusCycle&) const override { return false; }
    std::vector<Property> properties() override { return {}; }
    uint64_t rxBytes() const override {
        if (armed.exchange(false)) Debugger::requestStop();
        return 0;
    }
};

} // namespace

void test_mcp() {
    SECTION("MCP: the encoder never emits invalid JSON for guest bytes");
    {
        // A serial terminal is 8-bit clean and can print ANY byte -- a lone 0xFF, a
        // control code -- while a host path really is UTF-8. The one must be escaped,
        // the other must survive.
        std::string raw = "HI\xff\x01 caf\xc3\xa9";  // lone FF, ^A, then a valid UTF-8 e-acute
        std::string d = Json(raw).dump();
        CHECK(d.find("\\u00ff") != std::string::npos, "a lone 0xFF byte is \\u-escaped");
        CHECK(d.find("\\u0001") != std::string::npos, "a control byte is \\u-escaped");
        CHECK(d.find("\xc3\xa9") != std::string::npos, "a valid UTF-8 sequence passes through");
        CHECK(d.find('\xff') == std::string::npos, "no raw high byte survives to break the line");

        // ...and the result is valid JSON that a client can actually parse (an escaped
        // byte comes back as its code point, not the raw byte -- that is what keeps the
        // line legal).
        Json back;
        std::string err;
        CHECK(Json::parse(d, back, err), "the escaped form is valid JSON");
        CHECK(back.str().find("caf\xc3\xa9") != std::string::npos, "and the UTF-8 in it survived the trip");
    }

    SECTION("MCP: a \\uXXXX control byte decodes to that one raw byte for the guest");
    {
        // The INPUT direction of the section above, and the reason issue #491 was filed:
        // `send`/`run` hand their string straight to the console, so what a client may
        // type at the guest is decided here, in the parser. ^C and ^Z must arrive as one
        // byte each -- they are how a guest program is broken off.
        Json j;
        std::string err;
        CHECK(Json::parse(R"("A\u0003B\u001aC")", j, err), "a \\u-escaped control byte parses");
        const std::string in = j.str();
        CHECK(in.size() == 5, "each escape decodes to exactly one byte");
        CHECK(in == std::string("A\x03" "B\x1a" "C", 5), "and it is the byte the escape names");

        // THE TRAP: JSON has no \x escape. We are lenient with an unknown one (drop the
        // backslash, keep the letter) rather than rejecting the request, so `\x03` types
        // three ordinary characters at the guest. That looks exactly like "control bytes
        // are filtered out and printable text gets through", which is what it was read as.
        CHECK(Json::parse(R"("\x03")", j, err), "\\x03 is not rejected -- we are lenient");
        CHECK(j.str() == "x03", "it is NOT a control byte: it types the characters x03");
    }

    SECTION("MCP: interactive tools drive a running guest (MON680)");
    {
        Machine m;
        if (!loadAltair680(m)) return;

        std::ostringstream s;
        int id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/list", "{}");
        // MON680's reset entry is FFD8 (65496); it prints CR/LF and its `.` prompt.
        req("tools/call", R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req("tools/call", R"({"name":"regs","arguments":{}})");
        // Examine a ROM byte through the monitor's own M command: no `until`, let the guest
        // run to its next prompt (idle) so the whole examine line lands. Proves send + run +
        // recv move real bytes both ways.
        req("tools/call",
            std::string(R"({"name":"run","arguments":{"input":")") + kMon680Examine +
            R"(","timeout_ms":4000}})");

        auto rep = runScript(m, s.str());

        // tools/list carries the interactive four alongside the builders.
        bool run = false, send = false, recv = false, regs = false;
        for (const auto& t : rep[2].at("result").at("tools").items()) {
            std::string n = t.at("name").str();
            run  |= (n == "run");
            send |= (n == "send");
            recv |= (n == "recv");
            regs |= (n == "regs");
        }
        CHECK(run && send && recv && regs, "tools/list advertises run, send, recv and regs");

        const Json& boot = rep[3].at("result").at("structuredContent");
        CHECK(boot.at("stopped").str() == "match", "run boots to the prompt and stops on the match");
        CHECK(boot.at("output").str().find("\r\n.") != std::string::npos,
              "MON680 prints CR/LF and its `.` prompt (no banner)");

        const Json& r = rep[4].at("result").at("structuredContent");
        CHECK(r.has("pc") && r.has("registers"), "regs reports pc and the register file");

        const Json& dump = rep[5].at("result").at("structuredContent");
        CHECK(dump.at("output").str().find(kMon680ExamineEcho) != std::string::npos,
              "the M command's output is MON680's own ROM byte, read and printed by the guest");
    }

    SECTION("MCP: the monitor TYPE command reaches the guest console (issue #427)");
    {
        // TYPE injects type-ahead into Console::instance(), but under --mcp the guest's
        // console line is rebound to a headless ScriptedStream -- so TYPE via the `monitor`
        // tool used to vanish silently while `run`/`send` worked. It must now feed the same
        // scripted line the guest actually reads.
        Machine m;
        if (!loadAltair680(m)) return;

        std::ostringstream s;
        int id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/call", R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        // Type MON680's own examine command through the MONITOR tool's TYPE, NOT run's input.
        req("tools/call",
            std::string(R"({"name":"monitor","arguments":{"command":"TYPE \")") + kMon680Examine +
            R"(\""}})");
        // Then advance the guest with a bare run -- nothing typed here. If TYPE reached the
        // scripted line, the guest reads it now and the examine line comes out.
        req("tools/call", R"({"name":"run","arguments":{"timeout_ms":4000}})");

        auto rep = runScript(m, s.str());

        const Json& typed = rep[3].at("result");
        CHECK(!typed.has("isError"), "the TYPE command did not error");
        const Json& out = rep[4].at("result").at("structuredContent");
        CHECK(out.at("output").str().find(kMon680ExamineEcho) != std::string::npos,
              "the guest executed the TYPEd command -- its examine reached the scripted console");
    }

    SECTION("MCP: --mirror wraps the console so a socket client watches and takes over");
    {
        Machine m;
        if (!loadAltair680(m)) return;

        uint16_t port = freePort();
        CHECK(port != 0, "the OS hands us a free port");
        const std::string mirror = "socket:" + std::to_string(port);

        // Drive a real MCP session WITH the mirror: initialize rebinds the console to
        // scripted|socket:PORT, and the boot run makes the guest print its prompt.
        std::ostringstream s;
        int                id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/call",
            R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        auto rep = runScript(m, s.str(), mirror);

        // The assistant still sees the guest's output -- the inner scripted is driven
        // through the wrapper, transparently, so the run loop needed no change.
        const Json& boot = rep[2].at("result").at("structuredContent");
        CHECK(boot.at("output").str().find("\r\n.") != std::string::npos,
              "the assistant's run still sees the prompt through the mirror");

        // The console line is now a mirror over scripted, describing the socket sink.
        ScriptedStream* inner = nullptr;
        MirrorStream*   mir   = consoleMirror(m, &inner);
        CHECK(mir != nullptr, "the console unit is wrapped in a MirrorStream");
        if (mir)
            CHECK(mir->describe() == "scripted|" + mirror,
                  "and it describes itself as scripted|socket:PORT");
        CHECK(inner != nullptr, "the inner line is the scripted stream the tools drive");

        // A human telnets in AFTER the assistant started -- the listener is still open on
        // the unit -- and takes over: they type an examine command, the guest EXECUTES it,
        // and the result comes back down the same socket. Proves both directions end to end,
        // through the exact wiring --mcp --mirror builds.
        if (mir && inner) {
            std::string err;
            auto client = platform::connectTcp("127.0.0.1", port, err);
            CHECK(client != nullptr, ("a watcher dials in: " + err).c_str());
            bool up = waitFor([&] {
                m.pump();  // the run loop pumps every stream, the mirror among them
                if (client) client->poll();
                return client && client->established();
            });
            CHECK(up, "the watcher connects and the mirror accepts it mid-session");

            // The watcher types MON680's own examine command -- INJECTED as input the guest
            // reads (take-over), not fed through the assistant's channel.
            const std::string cmd = kMon680Examine;
            if (client) client->write((const uint8_t*)cmd.data(), cmd.size());

            std::string seen;
            waitFor([&] {
                if (client) client->poll();
                m.pump();
                m.debug.run(2000);  // give the guest cycles to read and execute
                m.pump();
                uint8_t b[256];
                size_t  r = client ? client->read(b, sizeof b) : 0;
                seen.append((const char*)b, r);
                return seen.find(kMon680ExamineEcho) != std::string::npos;
            });
            CHECK(seen.find(kMon680ExamineEcho) != std::string::npos,
                  "the watcher's typed command was executed by the guest, examine came back down the socket");
        }
    }

    SECTION("MCP: CONFIG LOAD via the monitor tool re-adopts the console and --mirror at once");
    {
        // Issue #481: CONFIG LOAD replaces every board, destroying the scripted console line
        // and the --mirror listener riding on it. Only send/recv/run re-adopted the console,
        // so until one of those ran the mirror port was closed and the new console unit was
        // back on the host console -- the JSON-RPC stdin. The monitor tool must re-adopt it
        // itself, before the next tool call of any kind.
        Machine m;
        if (!loadAltair680(m)) return;
        std::string err;

        const std::string toml = tmpPath("swtpc_mcp_reload.toml");
        {
            Monitor mon(m);
            std::ostringstream os;
            mon.exec("CONFIG SAVE " + toml, os);
            CHECK(!mon.failed(), ("CONFIG SAVE writes the machine: " + os.str()).c_str());
        }

        uint16_t port = freePort();
        CHECK(port != 0, "the OS hands us a free port");
        const std::string mirror = "socket:" + std::to_string(port);

        std::ostringstream s;
        s << R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" << "\n";
        s << R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"monitor",)"
          << R"("arguments":{"command":)" << Json("CONFIG LOAD " + toml).dump() << "}}}\n";
        auto rep = runScript(m, s.str(), mirror);

        CHECK(rep.count(2) && !rep[2].at("result").has("isError"),
              "CONFIG LOAD through the monitor tool succeeded");

        // No send/recv/run was called, yet the console is ours again, mirror and all.
        ScriptedStream* inner = nullptr;
        MirrorStream*   mir   = consoleMirror(m, &inner);
        CHECK(mir != nullptr, "the reloaded machine's console is wrapped in the mirror again");
        CHECK(inner != nullptr, "over the scripted line the tools drive, not the host console");

        // And the port is listening: a watcher can dial in right away.
        if (mir) {
            auto client = platform::connectTcp("127.0.0.1", port, err);
            CHECK(client != nullptr, ("a watcher dials in after the reload: " + err).c_str());
            bool up = waitFor([&] {
                m.pump();
                if (client) client->poll();
                return client && client->established();
            });
            CHECK(up, "the mirror accepts a connection with no send/recv/run in between");
        }
        std::filesystem::remove(toml);
    }

    SECTION("MCP: the console stand-in carries the machine's [console] transforms");
    {
        // The bug they hit on `ps2` (issue #381): under --mcp the console is a headless
        // scripted line, so strip7out/upper -- the console's transform chain -- were bypassed
        // and the assistant read bit-7 parity junk (0x4F 'O' came back 0xCF). The scripted
        // stand-in must wear the same filter a human console would.
        Machine m;
        if (!loadAltair680(m)) return;

        // Turn strip7out on the way a machine file's [console] block does -- the same property
        // seam SET CONSOLE and the TOML loader drive. Save/restore: the Console is a process
        // singleton shared with every other test.
        std::string err;
        bool        saved = false, savedUpper = false;
        auto setConsole = [&err](const char* name, bool on) {
            for (Property& p : Console::instance().properties())
                if (p.name == name) p.set(Value::ofBool(on), err);
        };
        for (Property& p : Console::instance().properties()) {
            if (p.name == "strip7out") saved = p.get().b();
            if (p.name == "upper") savedUpper = p.get().b();
        }
        setConsole("strip7out", true);
        setConsole("upper", false);

        std::ostringstream s;
        s << R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})" << "\n";
        s << R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"monitor",)"
          << R"("arguments":{"command":"SHOW CONSOLE"}}})" << "\n";
        auto rep = runScript(m, s.str());  // binds the console to Filter(scripted)

        // SHOW CONSOLE names the stand-in as the console's holder (issue #529): it used to
        // say "(nobody", which read as the transforms being out of the path.
        const std::string show =
            rep.count(2) ? rep[2].at("result").at("content").items().at(0).at("text").str()
                         : std::string();
        CHECK(show.find("(--mcp)") != std::string::npos && show.find("(nobody") == std::string::npos,
              "SHOW CONSOLE under --mcp names the console stand-in as the holder");

        // The console unit's stream is a FilterStream (outermost) over the scripted line.
        FilterStream*   filt = nullptr;
        ScriptedStream* sc   = nullptr;
        for (const auto& b : m.boards())
            for (const auto& u : b->units()) {
                if (u.kind != UnitKind::Serial) continue;
                if (auto* f = dynamic_cast<FilterStream*>(b->unitStream(u.name))) {
                    filt = f;
                    sc   = dynamic_cast<ScriptedStream*>(f->inner());
                }
            }
        CHECK(filt != nullptr, "the console line is wrapped in the console's transform filter");
        CHECK(sc != nullptr, "and the inner line is the scripted stream the tools drive");

        // A guest byte with bit 7 set is stripped on its way to the assistant's out().
        if (filt && sc) {
            uint8_t hi = 0xCF;  // 'O' (0x4F) with the even-parity bit set -- the ps2 case
            filt->write(&hi, 1);
            CHECK(sc->out() == std::string(1, (char)0x4F),
                  "strip7out reaches the assistant: 0xCF arrives as 0x4F, not junk");

            // A SET CONSOLE made AFTER the binding reaches the guest (issue #529): the
            // stand-in follows the console's settings, it does not hold a copy of them.
            uint8_t c = 0;
            setConsole("upper", true);
            sc->feed("a");
            CHECK(filt->read(&c, 1) == 1 && c == 'A',
                  "SET CONSOLE upper=on mid-session folds what the assistant types");
            setConsole("upper", false);
            sc->feed("a");
            CHECK(filt->read(&c, 1) == 1 && c == 'a',
                  "and upper=off mid-session stops folding it");
        }

        // Restore the singleton for the tests that follow.
        setConsole("strip7out", saved);
        setConsole("upper", savedUpper);
    }

    SECTION("MCP: a RUN via the monitor tool parks instead of wedging the server");
    {
        // A bare RUN -- or the RUN a CONFIG LOAD startup ends in -- would enter the
        // unbounded run loop, and the single-threaded server has no keyboard to press
        // ATTN, so the whole connection would hang forever. Under MCP, RUN must set PC
        // and return. We plant an unconditional JMP-to-self at 0 (6800: 7E 00 00 = JMP
        // $0000) so the run is genuinely infinite: WITHOUT the fix this test never returns
        // (a hang, not a failed CHECK).
        Machine m;
        if (!loadAltair680(m)) return;

        std::ostringstream s;
        int id = 0;
        auto req = [&](const char* method, const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << method
              << R"(","params":)" << params << "}\n";
        };
        req("initialize", "{}");
        req("tools/call", R"({"name":"mem_deposit","arguments":{"addr":0,"bytes":"7E 00 00"}})");
        req("tools/call", R"({"name":"monitor","arguments":{"command":"RUN 0"}})");
        // The server is still alive afterwards -- a later call gets a reply, which it
        // could not if RUN had wedged the read-eval loop.
        req("tools/call", R"({"name":"regs","arguments":{}})");

        auto rep = runScript(m, s.str());  // returns at all == the fix works

        const std::string run = rep[3].at("result").at("content").items().at(0).at("text").str();
        CHECK(run.find("PC set to 0000") != std::string::npos,
              "RUN under MCP parks the PC instead of entering the run loop");
        CHECK(run.find("run tool") != std::string::npos,
              "and it points the client at the non-blocking run tool");
        CHECK(rep.count(4) && rep[4].at("result").has("structuredContent"),
              "the server answered a later call -- RUN returned, it did not wedge");
    }

    SECTION("MCP: tools/list advertises the structured wrappers");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        auto rep = runScript(m,
            R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})""\n");
        std::map<std::string, bool> seen;
        for (const auto& t : rep[1].at("result").at("tools").items())
            seen[t.at("name").str()] = true;
        for (const char* n : {"step", "disasm", "mem_fill", "mem_search", "mem_save",
                              "breakpoints", "snapshot", "restore", "bus_irq", "bus_trace",
                              "mount", "connect"})
            CHECK(seen[n], (std::string("tools/list carries ") + n).c_str());
    }

    SECTION("MCP: disasm decodes through a non-invasive peek, with no CPU running");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        // MON680's own first bytes at FF00 (65280): the INCH entry is `BSR FF24` (8D 22).
        // Decoded straight out of ROM before a single instruction executes -- the stateless
        // 6800 disassembler.
        auto rep = runScript(m,
            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"disasm","arguments":{"addr":65280,"count":2,"cpu":"6800"}}})""\n");
        const Json& d = rep[1].at("result").at("structuredContent");
        const auto& lines = d.at("lines").items();
        CHECK(lines.size() == 2, "disasm returned two lines");
        CHECK(lines.at(0).at("addr").integer() == 65280, "first line is at FF00");
        CHECK(lines.at(0).at("text").str() == "BSR FF24", "and decodes BSR FF24");
        CHECK(lines.at(0).at("len").integer() == 2, "a two-byte instruction");
        CHECK(lines.at(1).at("addr").integer() == 65282, "the next line follows by its length");
    }

    SECTION("MCP: step advances the CPU and reports where it rested");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req(R"({"name":"step","arguments":{"count":3}})");
        auto rep = runScript(m, s.str());
        const Json& st = rep[2].at("result").at("structuredContent");
        CHECK(st.at("steps").integer() == 3, "step ran three instructions");
        CHECK(st.has("registers") && st.has("pc") && st.has("cycles"),
              "and reported the register file, pc and cycles");
        CHECK(st.at("stopped").str() == "steps", "it stopped on the count, not a WAI/breakpoint");
    }

    // AN ARGUMENT OF THE WRONG JSON TYPE IS REFUSED, NOT READ AS 0 (#579). JSON has no hex,
    // so "from":"0xFF00" is the natural mistake -- and it used to run from PC 0 with no
    // word. Every tool's arguments are checked against its own inputSchema first.
    SECTION("MCP: an argument of the wrong type, or a missing one, is refused by name (#579)");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        // Boot first, so the PC sits in the ROM: a `from` read as 0 would then show.
        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})"); // 1
        req(R"({"name":"run","arguments":{"from":"0xFF00","timeout_ms":100}})");       // 2
        req(R"({"name":"mem_dump","arguments":{"lo":"0x100","hi":271}})");            // 3
        req(R"({"name":"mem_dump","arguments":{"hi":15}})");                           // 4
        req(R"({"name":"mem_deposit","arguments":{"addr":256,"bytes":"00","rom":"true"}})"); // 5
        req(R"({"name":"mem_dump","arguments":{"lo":"256","hi":271}})");               // 6
        req(R"({"name":"regs","arguments":{}})");                                      // 7
        req(R"({"name":"mem_dump","arguments":{"lo":256,"hi":271}})");                 // 8
        auto rep = runScript(m, s.str());

        auto err = [&](int n) {
            const Json& r = rep[n].at("result");
            return r.at("isError").boolean() ? r.at("content").items()[0].at("text").str()
                                             : std::string();
        };
        const long long booted = rep[1].at("result").at("structuredContent").at("pc").integer();
        CHECK(err(2).find("`from` must be a JSON number") != std::string::npos &&
                  err(2).find("65280") != std::string::npos,
              "a hex string for `from` is refused, and the error gives the number to send");
        CHECK(rep[7].at("result").at("structuredContent").at("pc").integer() == booted,
              "...and nothing ran: the PC is where the boot left it, not 0 or FF00");
        CHECK(err(3).find("`lo` must be a JSON number") != std::string::npos &&
                  err(3).find("256") != std::string::npos,
              "mem_dump refuses a string `lo` instead of dumping address 0");
        CHECK(err(4).find("`lo` is required") != std::string::npos,
              "a missing required argument is refused, not read as 0");
        CHECK(err(5).find("`rom` must be true or false") != std::string::npos,
              "a boolean sent as a string is refused, not read as false");
        CHECK(err(6).find("write 256, not \"256\"") != std::string::npos,
              "a decimal string gets told to drop the quotes");
        CHECK(err(8).empty() && rep[8].at("result").at("structuredContent").at("lo").integer() == 256,
              "the same call with numbers works");
    }

    // SET BUS UNCLAIMED=WARN|HALT HOLDS UNDER MCP AS AT THE MONITOR (#580). HALT stops the
    // run with `unclaimed`; WARN returns the warning line in `warnings`; each run re-arms
    // the once-per-address de-dup, so a second run reports the address again.
    SECTION("MCP: run honours SET BUS UNCLAIMED=HALT and WARN (#580)");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        // 0040: LDAA $2000 ; BRA 0040 -- a guest polling an address no board on the 680b
        // decodes (its RAM is 0000-03FF).
        req(R"({"name":"monitor","arguments":{"command":"DEPOSIT 40 B6 20 00 20 FB"}})"); // 1
        req(R"({"name":"monitor","arguments":{"command":"SET BUS UNCLAIMED=HALT"}})");    // 2
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":2000}})");               // 3
        req(R"({"name":"monitor","arguments":{"command":"SET BUS UNCLAIMED=WARN"}})");    // 4
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":200}})");                // 5
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":200}})");                // 6
        auto rep = runScript(m, s.str());

        const Json& h = rep[3].at("result").at("structuredContent");
        CHECK(h.at("stopped").str() == "unclaimed", "HALT stops the run as `unclaimed`");
        CHECK(h.at("warnings").items().size() == 1 &&
                  h.at("warnings").items()[0].str().find("read 2000") != std::string::npos,
              "...and returns the warning line naming the address");
        const std::string ht = rep[3].at("result").at("content").items()[0].at("text").str();
        CHECK(ht.find("stopped: read from 0x2000, which no board decodes") != std::string::npos,
              "...and the text says which address, as the monitor does");

        for (int n : {5, 6}) {
            const Json& w = rep[n].at("result").at("structuredContent");
            CHECK(w.at("stopped").str() != "unclaimed", "WARN does not stop the run");
            CHECK(w.at("warnings").items().size() == 1 &&
                      w.at("warnings").items()[0].str().find("read 2000") != std::string::npos,
                  "WARN returns the line, and each run reports it again");
        }
    }

    // BREAK TAPE STOP stops an MCP run as it stops a monitor RUN -- the same slice-loop gap
    // as #580, found beside it.
    SECTION("MCP: run stops on BREAK TAPE STOP as `tape-stop`");
    {
        const auto tap = std::filesystem::temp_directory_path() / "swtpcsim-mcp-tapestop.tap";
        {
            std::ofstream f(tap, std::ios::binary);
            for (int i = 0; i < 300; ++i) f.put(char(i & 0xFF));
        }
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        auto mon = [&](const std::string& cmd) {
            req(R"({"name":"monitor","arguments":{"command":")" + cmd + R"("}})");
        };
        std::string path = tap.generic_string();
        mon("BOARDS ADD 680kcacr acr0");
        mon("MOUNT acr0:tape \\\"" + path + "\\\"");
        mon("SET acr0:tape stop=0:05");  // 150 bytes in, at 300 baud
        // 0040: LDAA $F010 ; RORA ; BCS 0040 ; LDAA $F011 ; BRA 0040 -- read the tape
        // forever (the KCACR's RDA is active-low: D0 = 1 means nothing yet).
        mon("DEPOSIT 40 B6 F0 10 46 25 FA B6 F0 11 20 F5");
        mon("BREAK TAPE STOP");
        req(R"({"name":"run","arguments":{"from":64,"timeout_ms":4000}})");  // 6
        auto rep = runScript(m, s.str());
        std::error_code ec;
        std::filesystem::remove(tap, ec);

        for (int n = 1; n <= 5; ++n)
            CHECK(!rep[n].at("result").at("isError").boolean(), "the tape set-up is accepted");
        CHECK(rep[6].at("result").at("structuredContent").at("stopped").str() == "tape-stop",
              "the run stops at the tape's auto-stop mark");
    }

    SECTION("MCP: run stops on a prompt as idle, and SET cpu0 idle=off keeps it running");
    {
        // MON680 boots to its `.` prompt and spins on console input (POLCAT/INCH). With the
        // CPU card's idle policy on (the default), `run` recognises that spin and hands
        // control back promptly with stopped=idle. `SET cpu0 idle=off` is how a
        // hardware-in-the-loop run says "do not park me" -- the same knob #424's guest set --
        // and then `run` runs the whole timeout budget instead. Both prove the m.clock.idle()
        // gate on the idle-stop.
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req(R"({"name":"run","arguments":{"timeout_ms":500}})");                 // at the prompt
        req(R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})");
        req(R"({"name":"run","arguments":{"timeout_ms":500}})");                 // no parking now
        auto rep = runScript(m, s.str());

        CHECK(rep[1].at("result").at("structuredContent").at("stopped").str() == "match",
              "the boot run stops on the prompt");
        CHECK(rep[2].at("result").at("structuredContent").at("stopped").str() == "idle",
              "at the prompt, idle=on -> run hands back with stopped=idle");
        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "idle=off -> the same prompt spin runs the whole budget (no early idle-stop)");
    }

    SECTION("MCP: a live device on a line defers the idle-stop by a wall-clock grace");
    {
        // Same MON680 prompt spin -- but with a real wire connected (loopback on a 680uio's
        // serial channel, fitted for the purpose), "quiet" might just be the guest between a request and a reply that
        // lands hundreds of ms later. So the idle-stop is not taken until the wire has been
        // silent for a wall-clock grace (seconds), far longer than this run's 500 ms budget --
        // where the no-device run above stopped=idle almost at once, this one runs the budget
        // out. This grace is what stops a request/reply gap or a between-blocks pause on a real
        // device from being mistaken for a finished prompt (#424); it is measured in WALL time,
        // so it holds at any clock_hz. It is scoped to idle-stop ONLY -- since #487, it does NOT
        // extend timeout_ms itself; see the next section for that boundary.
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"monitor","arguments":{"command":"BOARDS ADD 680uio uio0"}})");
        req(R"({"name":"connect","arguments":{"id":"uio0","unit":"serial","endpoint":"loopback"}})");
        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req(R"({"name":"run","arguments":{"timeout_ms":500}})");  // at the prompt, device on line
        auto rep = runScript(m, s.str());

        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "with a device on a line the prompt spin is not called idle within the budget");
    }

    SECTION("MCP: timeout_ms is a strict wall-clock ceiling even while a live wire keeps "
            "talking (#487)");
    {
        // #487: a version of this loop let a live wire's traffic renew the DEADLINE itself, not
        // just defer idle-stop (the section above) -- so a peer that said anything at all,
        // however slowly, kept `run` going past its budget indefinitely, bounded only by a
        // 10-minute absolute cap. A real TCP peer here writes one byte every 150 ms for 1.5 s
        // straight -- comfortably inside the 5 s grace idle-stop still uses -- against a 400 ms
        // budget. Pre-fix this call would not have returned until ~5 s after the writer's last
        // byte (~6.5 s total); fixed, it must return at ~400 ms regardless of the traffic.
        Machine m;
        if (!loadAltair680(m)) return;
        std::string err;

        uint16_t port = freePort();
        CHECK(port != 0, "the OS hands us a free port");

        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"monitor","arguments":{"command":"BOARDS ADD 680uio uio0"}})");
        req(R"({"name":"connect","arguments":{"id":"uio0","unit":"serial","endpoint":"socket:)" +
            std::to_string(port) + R"("}})");
        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req(R"({"name":"run","arguments":{"timeout_ms":400}})");  // the call under test

        // The peer connects AFTER the "connect" request above binds the listener, which happens
        // mid-script inside the runScript() call below -- so it races that bind and retries.
        std::atomic<bool> stopFeed{false};
        std::thread feeder([&] {
            std::string ferr;
            std::unique_ptr<platform::TcpConn> client;
            for (int i = 0; i < 400 && !client && !stopFeed; ++i) {
                client = platform::connectTcp("127.0.0.1", port, ferr);
                if (!client) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!client) return;
            for (int i = 0; i < 200 && !client->established() && !stopFeed; ++i) {
                client->poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const uint8_t byte = 'X';
            for (int i = 0; i < 10 && !stopFeed; ++i) {
                client->poll();
                client->write(&byte, 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        });

        const auto t0  = std::chrono::steady_clock::now();
        auto       rep = runScript(m, s.str());
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
        stopFeed = true;
        feeder.join();

        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "the budget run stops on timeout_ms, not idle or anything else");
        CHECK(elapsedMs < 1200,
              ("timeout_ms:400 held under continuous live-wire traffic (took " +
               std::to_string(elapsedMs) +
               "ms; the pre-#487 grace would have run past 1500ms of writes plus a further "
               "5s of post-traffic silence)")
                  .c_str());
    }

    SECTION("MCP: a SIGINT to the process interrupts a wedged run (#488)");
    {
        // #488: `--mcp`'s stdin IS the JSON-RPC channel, so a caller has no in-band way to
        // stop a run that will not reach `until`/idle/timeout on its own -- unlike a piped
        // monitor RUN, which already had SigintGuard (core/debug.h). Proves the guard is now
        // installed for the whole of runMcp too: a stop request lands mid-call (simulated with
        // std::raise, the portable way to invoke the currently-installed handler without
        // depending on OS-level signal delivery -- the handler itself, and the atomic flag it
        // sets, are exactly what a real SIGINT would drive) and the run returns almost at once
        // with stopped:"interrupted", nowhere near its multi-second budget. The trailing run
        // also proves `Debugger::clearStopRequest()` at the top of each call means the flag does
        // not bleed into the next one.
        Machine m;
        if (!loadAltair680(m)) return;

        auto r = runWithSigint(
            m,
            {R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})",
             R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})"},
            R"({"name":"run","arguments":{"timeout_ms":4000}})",          // the call under test
            {R"({"name":"run","arguments":{"timeout_ms":500}})"});        // stale-flag check
        auto& rep = r.replies;

        CHECK(rep[3].at("result").at("structuredContent").at("stopped").str() == "interrupted",
              "the 4000ms-budget run stops on the SIGINT, not idle or timeout");
        CHECK(r.elapsedMs < 1500,
              ("the stop request landed well inside the 4000ms budget (took " +
               std::to_string(r.elapsedMs) + "ms)")
                  .c_str());
        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "clearStopRequest() means the next run() does not inherit a stale flag -- idle=off "
              "still set, so it runs its own budget out exactly like the no-stop case");
    }

    SECTION("MCP: a SIGINT is not lost in the pacing sleep between slices (#488)");
    {
        // The case the flat-out test above cannot see. With a clock_hz set, `run` sleeps
        // between slices to pace the crystal, so most of its wall time is spent OUTSIDE
        // Debugger::run() -- and run() clears the stop-request flag as it enters. Before the
        // Debugger::stopRequested() check at the top of the loop, a ^C landing in that sleep
        // was wiped by the next slice: five of eight trials here returned `timeout` having
        // ignored the signal outright. Repeat the trial, because "usually stops" is exactly
        // the bug -- a cancellation that works two times in three is not a cancellation.
        for (int trial = 0; trial < 6; ++trial) {
            Machine m;
            if (!loadAltair680(m)) return;

            auto rep = runWithSigint(
                m,
                {R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})",
                 R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})",
                 R"({"name":"monitor","arguments":{"command":"SET cpu0 clock_hz=2000000"}})"},
                R"({"name":"run","arguments":{"timeout_ms":3000}})",  // the call under test
                {}).replies;

            CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "interrupted",
                  ("a paced run stops on the SIGINT every time, not just when it happens to "
                   "land inside a slice (trial " + std::to_string(trial) + ")").c_str());
        }
    }

    SECTION("MCP: notifications/cancelled interrupts the matching in-flight run, and other "
            "requests queue behind it correctly (#488)");
    {
        // #488, part 2: with a static istringstream (runScript's usual approach) every line
        // is available to the reader thread at once, so a cancellation would race straight
        // past the `run` it targets before that call even starts -- nothing paces it against
        // real time the way a live client's stdin does. FeedBuf (above) fixes that: this test
        // controls exactly when each line becomes visible, matching how a real client would
        // actually drive this (send the call, then LATER send the cancellation once something
        // -- a user hitting Ctrl-C in their own tool -- decides to cancel it).
        //
        // Also proves the other half of the reader/worker split in the same flow: `regs` and
        // `recv`, fed WHILE the long run is still executing, must queue and still be answered
        // correctly once the worker frees up -- a busy worker must never block the reader from
        // accepting more requests, only from EXECUTING them out of order.
        Machine m;
        if (!loadAltair680(m)) return;

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);

        std::thread worker([&] { runMcp(m, feedIn, out, ""); });

        int  id  = 0;
        auto req = [&](const std::string& params) {
            std::ostringstream line;
            line << R"({"jsonrpc":"2.0","id":)" << ++id
                 << R"(,"method":"tools/call","params":)" << params << "}\n";
            feedBuf.feed(line.str());
        };

        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req(R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})");

        // Wait for the setup to ANSWER before sending the call to be cancelled: a cancel is
        // matched against the request in flight, so one that arrives while the guest is still
        // booting names an id that is not current yet and is dropped -- which is right, and
        // would make this a test of how fast the runner is rather than of the cancel.
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(2) != 0; }, 30000),
              "the setup calls answered before the run to be cancelled was sent");

        CHECK(waitForWorker(feedBuf, sinkBuf, false),
              "the worker is idle again before the run to be cancelled is fed");
        req(R"({"name":"run","arguments":{"timeout_ms":4000}})");  // id 3 -- will be cancelled
        // A cancel is matched against `currentId`, so id 3 must BE the in-flight request
        // before it is sent -- not merely fed. Waiting on that is the difference between
        // testing the cancel and testing how promptly the runner scheduled a thread.
        CHECK(waitForWorker(feedBuf, sinkBuf, true), "id 3 is the in-flight request");

        // Queued immediately behind the still-running id 3 -- proves the reader thread is not
        // blocked by a busy worker, regardless of when the worker actually gets to them.
        req(R"({"name":"regs","arguments":{}})");  // id 4
        req(R"({"name":"recv","arguments":{}})");  // id 5

        const auto t0 = std::chrono::steady_clock::now();
        feedBuf.feed(R"({"jsonrpc":"2.0","method":"notifications/cancelled",)"
                     R"("params":{"requestId":3}})"
                     "\n");

        // Stop the clock when id 3 ANSWERS. Measuring past here would bill the cancel for
        // id 6's own 500ms budget and the join, which have nothing to do with how promptly
        // the cancel landed.
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(3) != 0; }, 30000),
              "the cancelled run answered");
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();

        req(R"({"name":"run","arguments":{"timeout_ms":500}})");  // id 6 -- stale-flag check
        feedBuf.close();
        worker.join();

        std::map<int, Json> rep = repliesById(sinkBuf.text());

        CHECK(rep[3].at("result").at("structuredContent").at("stopped").str() == "interrupted",
              "notifications/cancelled for id 3 stops the matching in-flight run");
        CHECK(elapsedMs < 1500,
              ("the cancel landed well inside the 4000ms budget (took " +
               std::to_string(elapsedMs) + "ms)")
                  .c_str());
        CHECK(rep[4].at("result").at("structuredContent").has("pc"),
              "regs, queued behind the busy run, still answers once the worker is free");
        CHECK(rep[5].at("result").at("structuredContent").has("output"),
              "recv, queued right behind it, answers too -- in order, not dropped");
        CHECK(rep[6].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "id 6 runs its own budget out normally -- the cancel for id 3 does not leak "
              "into a later, unrelated request");
    }

    SECTION("MCP: status answers out of band, from a published slice-boundary snapshot, "
            "even while a run is mid-flight (#490)");
    {
        // #490: adding `status` as an ordinary tool would not make it non-blocking -- it
        // would just queue behind the very run it exists to report on, exactly like `regs`/
        // `recv` in the section above. This proves the actual fix: `status` is answered by
        // the READER thread, straight from the snapshot the `run` tool publishes at each
        // slice boundary (RunSnapshot, statusResult()), so it lands even while a long run
        // still sits busy in the worker.
        Machine m;
        if (!loadAltair680(m)) return;

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        // SinkBuf, not an ostringstream: waitForWorker() reads this WHILE the server is
        // writing it, and that is a data race on a bare stringstream.
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);

        std::thread worker([&] { runMcp(m, feedIn, out, ""); });

        int  id  = 0;
        auto req = [&](const std::string& params) {
            std::ostringstream line;
            line << R"({"jsonrpc":"2.0","id":)" << ++id
                 << R"(,"method":"tools/call","params":)" << params << "}\n";
            feedBuf.feed(line.str());
        };

        req(R"({"name":"status","arguments":{}})");  // id 1 -- before anything has run
        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req(R"({"name":"monitor","arguments":{"command":"SET cpu0 idle=off"}})");

        // The setup has to FINISH before id 4 is fed. The boot (id 2) and the SET (id 3) are
        // calls too, so while they run `in_flight` is already true -- wait for "busy" with them
        // still queued and it is the boot that answers, not the run this section is about.
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(3) != 0; }, 30000),
              "the setup calls answered before the long run was fed");
        CHECK(waitForWorker(feedBuf, sinkBuf, false), "the worker is idle before id 4 is fed");

        // id 4: flat out, no `until`, idle disabled -- runs its full 1500ms budget, giving a
        // wide window (two 300ms-spaced polls below, with 900ms of margin left over) to poll
        // `status` while it is genuinely still executing. 1500ms proves the same thing as a
        // longer budget without adding several seconds to every run of this suite.
        req(R"({"name":"run","arguments":{"timeout_ms":1500}})");

        // Poll only once id 4 is genuinely the in-flight call. The 300ms BETWEEN the two
        // polls is real and deliberate -- `steps`/`generation` have to be given time to
        // advance -- but "id 4 has started" is a fact to read off the server, not to sleep
        // for (waitForWorker, above).
        CHECK(waitForWorker(feedBuf, sinkBuf, true), "id 4 is the in-flight call before it is polled");
        req(R"({"name":"status","arguments":{}})");  // id 5 -- id 4 is still running
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        req(R"({"name":"status","arguments":{}})");  // id 6 -- later still, id 4 still running

        feedBuf.close();
        worker.join();

        std::map<int, Json> rep;
        std::vector<int>    order;  // the order replies were WRITTEN, not the order requested
        std::istringstream  lines(sinkBuf.text());
        std::string         line;
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            Json        j;
            std::string err;
            if (Json::parse(line, j, err)) {
                int rid = (int)j.at("id").integer();
                rep[rid] = j;
                order.push_back(rid);
            }
        }

        const Json& s1 = rep[1].at("result").at("structuredContent");
        CHECK(!s1.at("in_flight").boolean() && s1.at("steps").integer() == 0,
              "status before any run has happened: idle, zero steps");
        CHECK(!s1.at("board").str().empty(),
              "status knows the CPU board's id even before the first run (#490)");

        const Json& s5 = rep[5].at("result").at("structuredContent");
        const Json& s6 = rep[6].at("result").at("structuredContent");
        CHECK(s5.at("in_flight").boolean() && s6.at("in_flight").boolean(),
              "status reports in_flight while the long run (id 4) is still executing");
        CHECK(s6.at("steps").integer() > s5.at("steps").integer(),
              "the published snapshot keeps advancing between two polls of the same live run");
        CHECK(s6.at("generation").integer() > s5.at("generation").integer(),
              "the generation counter advances too, so a poller can tell 'still running' from "
              "'stuck on the same slice'");

        auto posOf = [&](int rid) {
            return (size_t)(std::find(order.begin(), order.end(), rid) - order.begin());
        };
        CHECK(posOf(5) < posOf(4) && posOf(6) < posOf(4),
              "both mid-run status replies (id 5, id 6) are WRITTEN before id 4's own reply -- "
              "proof they were answered out of band, not queued behind it");

        CHECK(rep[4].at("result").at("structuredContent").at("stopped").str() == "timeout",
              "the long run (idle=off, no until) still runs its own budget out normally -- "
              "status polling alongside it changes nothing about how it stops");
    }

    SECTION("MCP: status reports in_flight for a long NON-run tool too, not just run (#490)");
    {
        // The case `status` exists to catch, and the one a `run`-only in_flight was blind
        // to: the worker is wedged inside some OTHER tool. `monitor STEP <n>` is that tool
        // here -- it advances the guest inside the worker for about a second and publishes
        // no snapshot at all, so `generation` stays 0 while it runs. An `in_flight` sourced
        // from the run snapshot would say "idle" throughout.
        Machine m;
        if (!loadAltair680(m)) return;

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        // SinkBuf, not an ostringstream: waitForWorker() reads this WHILE the server is
        // writing it, and that is a data race on a bare stringstream.
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);

        std::thread worker([&] { runMcp(m, feedIn, out, ""); });

        int  id  = 0;
        auto req = [&](const std::string& params) {
            std::ostringstream line;
            line << R"({"jsonrpc":"2.0","id":)" << ++id
                 << R"(,"method":"tools/call","params":)" << params << "}\n";
            feedBuf.feed(line.str());
        };

        req(R"({"name":"status","arguments":{}})");  // id 1 -- nothing running yet
        // id 2: ~10M single steps through the debugger, roughly a second of worker time.
        req(R"({"name":"monitor","arguments":{"command":"STEP 10000000"}})");
        CHECK(waitForWorker(feedBuf, sinkBuf, true),
              "id 2 is the in-flight call -- read off the server, not slept for");
        req(R"({"name":"status","arguments":{}})");  // id 3 -- id 2 is still stepping

        feedBuf.close();
        worker.join();

        std::map<int, Json> rep;
        std::vector<int>    order;
        std::istringstream  lines(sinkBuf.text());
        std::string         line;
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            Json        j;
            std::string err;
            if (Json::parse(line, j, err)) {
                rep[(int)j.at("id").integer()] = j;
                order.push_back((int)j.at("id").integer());
            }
        }

        const Json& s1 = rep[1].at("result").at("structuredContent");
        const Json& s3 = rep[3].at("result").at("structuredContent");
        CHECK(!s1.at("in_flight").boolean(), "status is idle before the monitor call starts");
        CHECK(s3.at("in_flight").boolean(),
              "status reports in_flight while the worker is inside a long `monitor`, not a run");
        CHECK(s3.at("generation").integer() == 0,
              "and it says so with no run snapshot ever published -- in_flight cannot be "
              "coming from the `run` tool's own bookkeeping");

        auto posOf = [&](int rid) {
            return (size_t)(std::find(order.begin(), order.end(), rid) - order.begin());
        };
        CHECK(posOf(3) < posOf(2),
              "the mid-monitor status reply is written before the monitor's own -- answered "
              "out of band, not queued behind it");
    }

    SECTION("MCP: a stop request that lands between the loop's check and the slice is not "
            "erased");
    {
        // The slice used to clear the stop-request flag on entry, so a cancel or ^C landing just
        // after the loop's own check was wiped unseen and the run served its whole budget --
        // `timeout`, where the client had asked it to stop. Windows CI hit it on #506 and
        // #508; 5000 tries there lost 9 cancels before the fix and none after. GapBoard (above)
        // puts the stop request in that window every time, so this fails every time without the
        // fix rather than once in 550.
        Machine m;
        if (!loadAltair680(m)) return;
        auto gb = std::make_unique<GapBoard>();
        gb->id  = "gap0";
        GapBoard* gap = gb.get();
        m.adopt(std::move(gb));

        FeedBuf      feedBuf;
        std::istream feedIn(&feedBuf);
        SinkBuf      sinkBuf;
        std::ostream out(&sinkBuf);
        std::thread  worker([&] { runMcp(m, feedIn, out, ""); });

        feedBuf.feed(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"run",)"
                     R"("arguments":{"from":65496,"until":".","timeout_ms":4000}}})"
                     "\n");
        feedBuf.feed(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"monitor",)"
                     R"("arguments":{"command":"SET cpu0 idle=off"}}})"
                     "\n");
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(2) != 0; }, 30000),
              "the setup answered before the board was armed");
        CHECK(waitForWorker(feedBuf, sinkBuf, false), "the worker is idle when the board is armed");

        gap->armed = true;
        feedBuf.feed(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"run",)"
                     R"("arguments":{"timeout_ms":1000}}})"
                     "\n");
        CHECK(waitFor([&] { return repliesById(sinkBuf.text()).count(3) != 0; }, 30000),
              "the run answered");
        feedBuf.close();
        worker.join();

        CHECK(!gap->armed, "the board did fire -- the loop read rxBytes() during the run");
        const std::string st =
            repliesById(sinkBuf.text())[3].at("result").at("structuredContent").at("stopped").str();
        CHECK(st == "interrupted",
              ("a stop request raised between the check and the slice stops the run (stopped=" +
               st + ")")
                  .c_str());
    }

    SECTION("MCP: mem_fill, mem_search and mem_save round-trip through the bus");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::string hex = tmpPath("swtpc_mcp_dump.hex");
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        // The 680b's onboard RAM is 1K at 0000-03FF; work inside it (0100-010F). 0x2000 would
        // float on this machine -- an empty socket, not RAM.
        req(R"({"name":"mem_fill","arguments":{"lo":256,"hi":271,"byte":171}})");          // AB x16
        req(R"({"name":"mem_deposit","arguments":{"addr":264,"bytes":"DE AD BE EF"}})");
        req(R"({"name":"mem_search","arguments":{"lo":0,"hi":1024,"bytes":"DE AD BE EF"}})");
        req(R"({"name":"mem_save","arguments":{"path":")" + hex + R"(","lo":256,"hi":271}})");
        req(R"({"name":"mem_fill","arguments":{"lo":256,"hi":271,"byte":0}})");             // wipe
        req(R"({"name":"mem_load","arguments":{"path":")" + hex + R"("}})");                 // reload
        req(R"({"name":"mem_dump","arguments":{"lo":264,"hi":267}})");
        auto rep = runScript(m, s.str());

        CHECK(rep[1].at("result").at("structuredContent").at("written").integer() == 16,
              "mem_fill wrote sixteen cells");
        CHECK(rep[1].at("result").at("structuredContent").at("discarded").integer() == 0,
              "all sixteen landed (RAM, not ROM)");
        const Json& srch = rep[3].at("result").at("structuredContent");
        CHECK(srch.at("count").integer() == 1 && srch.at("matches").items().at(0).integer() == 264,
              "mem_search finds the deposited pattern at 0108");
        CHECK(rep[4].at("result").at("structuredContent").at("format").str() == "HEX",
              "mem_save chose HEX from the .hex name");
        const auto& bytes = rep[7].at("result").at("structuredContent").at("bytes").items();
        CHECK(bytes.size() == 4 && bytes.at(0).integer() == 0xDE && bytes.at(3).integer() == 0xEF,
              "the saved HEX reloaded byte-for-byte after a wipe");
        std::filesystem::remove(hex);
    }

    SECTION("MCP: breakpoints add, list and remove");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"breakpoints","arguments":{"action":"add","kind":"pc","lo":256}})");
        req(R"({"name":"breakpoints","arguments":{}})");                     // list
        req(R"({"name":"breakpoints","arguments":{"action":"remove","id":1}})");
        req(R"({"name":"breakpoints","arguments":{}})");                     // list again
        auto rep = runScript(m, s.str());

        const Json& added = rep[1].at("result").at("structuredContent");
        CHECK(added.at("kind").str() == "pc" && added.at("lo").integer() == 256,
              "add returns the new PC breakpoint at 0100");
        CHECK(rep[2].at("result").at("structuredContent").at("breakpoints").items().size() == 1,
              "the list shows one breakpoint");
        CHECK(rep[4].at("result").at("structuredContent").at("breakpoints").items().empty(),
              "and none after remove");
    }

    SECTION("MCP: snapshot then restore round-trips machine state");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::string path = tmpPath("swtpc_mcp.state");
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id
              << R"(,"method":"tools/call","params":)" << params << "}\n";
        };
        req(R"({"name":"snapshot","arguments":{"path":")" + path + R"("}})");
        req(R"({"name":"restore","arguments":{"path":")" + path + R"("}})");
        auto rep = runScript(m, s.str());
        CHECK(rep[1].at("result").at("structuredContent").at("ok").boolean(), "snapshot wrote");
        CHECK(rep[2].at("result").at("structuredContent").at("ok").boolean(),
              "restore read it back into the matching machine");
        std::filesystem::remove(path);
    }

    SECTION("MCP: bus_irq and bus_trace are well-formed");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << "tools/call"
              << R"(","params":)" << params << "}\n";
        };
        req(R"({"name":"run","arguments":{"from":65496,"until":".","timeout_ms":4000}})");
        req(R"({"name":"bus_irq","arguments":{}})");
        req(R"({"name":"bus_trace","arguments":{"count":8}})");
        auto rep = runScript(m, s.str());

        const Json& irq = rep[2].at("result").at("structuredContent");
        CHECK(irq.has("int_pending") && irq.at("vectors").items().size() == 4,
              "bus_irq reports the IRQ line and the four 6800 vectors");
        const Json& tr = rep[3].at("result").at("structuredContent");
        CHECK(!tr.at("cycles").items().empty(), "bus_trace holds cycles from the run");
        const Json& c0 = tr.at("cycles").items().at(0);
        CHECK(c0.has("addr") && c0.has("type") && c0.at("master").str() == "cpu",
              "a cycle carries an address, a type and its master");
    }

    SECTION("MCP: connect wires a serial unit; mount rejects an unknown board");
    {
        Machine m;
        if (!loadAltair680(m)) return;
        std::ostringstream s;
        int id = 0;
        auto req = [&](const std::string& params) {
            s << R"({"jsonrpc":"2.0","id":)" << ++id << R"(,"method":")" << "tools/call"
              << R"(","params":)" << params << "}\n";
        };
        // The 680io console line is io0:tty -- wire it to a loopback endpoint.
        req(R"({"name":"connect","arguments":{"id":"io0","unit":"tty","endpoint":"loopback"}})");
        req(R"({"name":"mount","arguments":{"id":"nope","unit":"x","path":"/dev/null"}})");
        auto rep = runScript(m, s.str());
        const Json& con = rep[1].at("result").at("structuredContent");
        CHECK(con.at("id").str() == "io0" && con.at("endpoint").str() == "loopback",
              "connect reports the wiring it made");
        CHECK(rep[2].at("result").has("isError") && rep[2].at("result").at("isError").boolean(),
              "mount on an unknown board is an error, not a silent no-op");
    }
}
