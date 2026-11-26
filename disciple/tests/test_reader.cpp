//
// Tests for a reader's whole life, on inkerface's desktop backend: the
// real reader, the real inkerface network, sockets, storage and panel,
// stepped the way its main loop steps it.
//
// The other side is played with plain POSIX sockets on loopback, from a
// second thread: a stand-in Apostle that answers discovery and speaks the
// protocol, and a phone that walks through Wi-Fi setup over HTTP.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/reader.h"
#include "disciple/screens.h"
#include "disciple/wire.h"

#include <inkerface/display.h>
#include <inkerface/network.h>
#include <inkerface/storage.h>
#include <inkerface/surface.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using namespace disciple;
using namespace std::chrono_literals;

int failures = 0;

void check(bool passed, const std::string &what) {
    std::cout << (passed ? "PASS  " : "FAIL  ") << what << "\n";
    if (!passed) {
        ++failures;
    }
}

// Long enough for anything on loopback, short enough that a broken test
// fails rather than hangs.
constexpr int wait_ms = 5000;

bool has(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

//
// Step a reader as its main loop would, until done() or a few seconds.
//
bool step_until(reader &disciple, const std::function<bool()> &done) {
    const auto give_up = std::chrono::steady_clock::now() + 8s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > give_up) {
            return false;
        }
        disciple.step({});
        std::this_thread::sleep_for(std::min(disciple.wait(), 5ms));
    }
    return true;
}

//
// Bind a socket to an ephemeral loopback port and report the port.
//
int bind_loopback(int type, int &port) {
    const int descriptor = socket(AF_INET, type | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    socklen_t size = sizeof(address);
    getsockname(descriptor, reinterpret_cast<sockaddr *>(&address), &size);
    port = ntohs(address.sin_port);
    return descriptor;
}

int spare_port() {
    int port = 0;
    close(bind_loopback(SOCK_STREAM, port));
    return port;
}

//
// Read one whole protocol message from a TCP socket.
//
bool read_message(int descriptor, std::vector<std::uint8_t> &inbound,
                  message_type &type, std::string &payload) {
    for (;;) {
        std::size_t offset = 0;
        std::size_t size = 0;
        std::string error;
        const decode_result state = decode_message(
            inbound.data(), inbound.size(), type, offset, size, error);
        if (state == decode_result::failed) {
            return false;
        }
        if (state == decode_result::done) {
            payload.assign(reinterpret_cast<const char *>(inbound.data() + offset),
                           size);
            inbound.erase(inbound.begin(),
                          inbound.begin() +
                              static_cast<std::ptrdiff_t>(offset + size));
            return true;
        }
        pollfd entry{descriptor, POLLIN, 0};
        if (poll(&entry, 1, wait_ms) <= 0) {
            return false;
        }
        std::uint8_t buffer[4096];
        const ssize_t got = recv(descriptor, buffer, sizeof(buffer), 0);
        if (got <= 0) {
            return false;
        }
        inbound.insert(inbound.end(), buffer, buffer + got);
    }
}

//
// A stand-in Apostle: a discovery responder and a broadcast listener.
//
struct fake_apostle {
    int udp = -1;
    int udp_port = 0;
    int listener = -1;
    int tcp_port = 0;
    int discovers = 0;
    std::string hello;

    fake_apostle() {
        udp = bind_loopback(SOCK_DGRAM, udp_port);
        listener = bind_loopback(SOCK_STREAM, tcp_port);
        listen(listener, 4);
    }

    ~fake_apostle() {
        close(udp);
        close(listener);
    }

    //
    // Answer discovers until a reader connects, then return it.
    //
    int accept_reader() {
        for (;;) {
            pollfd entries[2] = {{listener, POLLIN, 0}, {udp, POLLIN, 0}};
            if (poll(entries, 2, wait_ms) <= 0) {
                return -1;
            }
            if ((entries[0].revents & POLLIN) != 0) {
                return accept(listener, nullptr, nullptr);
            }
            answer_one_discover();
        }
    }

    void answer_one_discover() {
        std::uint8_t buffer[2048];
        sockaddr_in from{};
        socklen_t size = sizeof(from);
        const ssize_t got = recvfrom(udp, buffer, sizeof(buffer), 0,
                                     reinterpret_cast<sockaddr *>(&from), &size);
        message_type type = message_type::ping;
        std::size_t offset = 0;
        std::size_t length = 0;
        std::string error;
        if (got <= 0 ||
            decode_message(buffer, static_cast<std::size_t>(got), type, offset,
                           length, error) != decode_result::done ||
            type != message_type::discover) {
            return;
        }
        ++discovers;
        std::vector<std::uint8_t> announce;
        encode_message(message_type::announce,
                       "{\"name\":\"apostle\",\"protocol\":1,\"port\":" +
                           std::to_string(tcp_port) + ",\"host\":\"fake\"}",
                       announce);
        sendto(udp, announce.data(), announce.size(), 0,
               reinterpret_cast<sockaddr *>(&from), size);
    }

    //
    // Take the hello, say welcome, and optionally send a page with it.
    //
    bool welcome(int peer, std::vector<std::uint8_t> &inbound,
                 const std::vector<std::uint8_t> *page, std::uint32_t sequence) {
        message_type type = message_type::ping;
        if (!read_message(peer, inbound, type, hello) ||
            type != message_type::hello) {
            return false;
        }
        std::vector<std::uint8_t> out;
        encode_message(message_type::welcome,
                       "{\"ok\":true,\"clientId\":\"c001\",\"keepalive\":30}",
                       out);
        if (page != nullptr) {
            append_page(out, *page, sequence);
        }
        return send(peer, out.data(), out.size(), MSG_NOSIGNAL) ==
               static_cast<ssize_t>(out.size());
    }

    static void append_page(std::vector<std::uint8_t> &out,
                            const std::vector<std::uint8_t> &page,
                            std::uint32_t sequence) {
        frame_header header;
        header.sequence = sequence;
        header.width = 480;
        header.height = 800;
        header.format = frame_format::gray_packed;
        header.gray_levels = 2;
        std::vector<std::uint8_t> payload;
        encode_frame_header(header, payload);
        payload.insert(payload.end(), page.begin(), page.end());
        encode_message(message_type::frame, payload.data(), payload.size(), out);
    }

    static bool send_page(int peer, const std::vector<std::uint8_t> &page,
                          std::uint32_t sequence) {
        std::vector<std::uint8_t> out;
        append_page(out, page, sequence);
        return send(peer, out.data(), out.size(), MSG_NOSIGNAL) ==
               static_cast<ssize_t>(out.size());
    }

    static bool acked(int peer, std::vector<std::uint8_t> &inbound,
                      std::uint32_t sequence) {
        message_type type = message_type::ping;
        std::string payload;
        while (read_message(peer, inbound, type, payload)) {
            long long value = 0;
            if (type == message_type::ack &&
                json_number(payload, "sequence", value) && value == sequence) {
                return true;
            }
        }
        return false;
    }
};

//
// A page all white but its first few pixels, in the given byte.
//
std::vector<std::uint8_t> page_starting(std::uint8_t first) {
    std::vector<std::uint8_t> page(480 / 8 * 800, 0xFF);
    page[0] = first;
    return page;
}

bool black(inkerface::display &screen, int x, int y) {
    const auto pixel = screen.frame().pixel_at(
        {static_cast<inkerface::coord>(x), static_cast<inkerface::coord>(y)});
    return pixel.has_value() && *pixel == inkerface::color::black;
}

//
// Already on a network: splash, discovery, a page, Apostle gone and back,
// and the next page, never covered by a status screen.
//
void test_discovery_and_pages() {
    inkerface::display screen;
    inkerface::network net;
    inkerface::storage disks;
    static_cast<void>(net.connect("", ""));
    panel view(screen);

    fake_apostle apostle;
    bool script_ok = false;
    std::thread daemon([&]() {
        std::vector<std::uint8_t> inbound;
        const std::vector<std::uint8_t> first = page_starting(0x00);
        int peer = apostle.accept_reader();
        if (peer < 0 || !apostle.welcome(peer, inbound, nullptr, 0)) {
            return;
        }
        std::this_thread::sleep_for(300ms);
        if (!fake_apostle::send_page(peer, first, 1) ||
            !fake_apostle::acked(peer, inbound, 1)) {
            close(peer);
            return;
        }
        close(peer);

        // Back, with the page in the same write as the welcome.
        inbound.clear();
        const std::vector<std::uint8_t> second = page_starting(0x0F);
        peer = apostle.accept_reader();
        if (peer < 0 || !apostle.welcome(peer, inbound, &second, 2) ||
            !fake_apostle::acked(peer, inbound, 2)) {
            close(peer);
            return;
        }
        script_ok = true;
        message_type type = message_type::ping;
        std::string payload;
        while (read_message(peer, inbound, type, payload) &&
               type != message_type::bye) {
        }
        close(peer);
    });

    reader_options options;
    options.port = apostle.udp_port;
    options.splash_ms = 50;
    std::ostringstream log;
    bool second_page = false;
    {
        reader disciple(net, disks, view, options, log);
        disciple.start();
        second_page = step_until(disciple, [&]() {
            // The second page: 0x0F is four black pixels, then white.
            return view.showing_page() && black(screen, 0, 0) &&
                   !black(screen, 4, 0);
        });
    }
    daemon.join();

    check(second_page, "the reader showed the second page");
    check(script_ok, "the daemon saw a hello, two acks, and a rejoin");
    check(apostle.discovers >= 2, "the reader found the daemon by discovery, twice");
    check(has(apostle.hello, "\"accepts\":[\"image/gray-packed\"]") &&
              has(apostle.hello, "\"width\":480") &&
              has(apostle.hello, "\"grayLevels\":2") &&
              has(apostle.hello, "\"device\":\"sheep_"),
          "the hello asks for one bit pages for the display's own size");
    const std::string text = log.str();
    check(has(text, "joined") && has(text, "lost"),
          "joining and losing the daemon are both reported");
    if (failures != 0) {
        std::cout << text;
    }
}

//
// One HTTP exchange from the phone. The reader listens on every
// interface, so the phone reaches it on loopback whatever its address.
//
std::string fetch(int port, const std::string &request) {
    const auto give_up = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < give_up) {
        const int peer = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(peer, reinterpret_cast<sockaddr *>(&address),
                    sizeof(address)) != 0) {
            close(peer);
            std::this_thread::sleep_for(20ms);
            continue;
        }
        send(peer, request.data(), request.size(), MSG_NOSIGNAL);
        std::string response;
        char buffer[4096];
        pollfd entry{peer, POLLIN, 0};
        while (poll(&entry, 1, 3000) > 0) {
            const ssize_t got = recv(peer, buffer, sizeof(buffer), 0);
            if (got <= 0) {
                break;
            }
            response.append(buffer, static_cast<std::size_t>(got));
        }
        close(peer);
        return response;
    }
    return "";
}

std::string post(int port, const std::string &host, const std::string &body) {
    return fetch(port, "POST /join HTTP/1.1\r\nHost: " + host +
                           "\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\n\r\n" + body);
}

//
// No network yet: the reader becomes its own access point, a phone sets
// it up, and the choice and the reader's name survive a power cycle.
//
void test_wifi_setup() {
    // Emulated internal flash in a fresh folder, so settings persist.
    char folder[] = "/tmp/disciple-test-XXXXXX";
    const char *root = mkdtemp(folder);
    setenv("INKERFACE_STORAGE_ROOT", root, 1);

    inkerface::display screen;
    inkerface::storage disks;
    panel view(screen);
    const int port = spare_port();

    std::vector<std::string> replies;
    std::thread phone([&]() {
        // A phone's internet check is redirected; the redirect says where
        // the reader is.
        replies.push_back(fetch(port, "GET /generate_204 HTTP/1.1\r\nHost: "
                                      "connectivitycheck.gstatic.com\r\n\r\n"));
        const std::size_t at = replies[0].find("Location: http://");
        const std::size_t end = replies[0].find("/\r\n", at);
        if (at == std::string::npos || end == std::string::npos) {
            return;
        }
        const std::string host = replies[0].substr(at + 17, end - at - 17);
        replies.push_back(
            fetch(port, "GET / HTTP/1.1\r\nHost: " + host + "\r\n\r\n"));
        replies.push_back(post(port, host, "network=Office&password=short"));
        replies.push_back(post(port, host, "network=Office&password=sing+along"));
    });

    reader_options options;
    options.port = spare_port();
    options.splash_ms = 0;
    options.setup_port = port;
    std::ostringstream log;
    std::string first_name;
    bool online = false;
    {
        inkerface::network net;
        reader disciple(net, disks, view, options, log);
        first_name = disciple.name();
        disciple.start();
        online = step_until(disciple, [&]() {
            return has(log.str(), "joined Office") && net.connected();
        });
    }
    phone.join();
    replies.resize(4);

    check(has(replies[0], "302 Found"), "the phone's internet check is redirected");
    check(has(replies[1], "200 OK") && has(replies[1], first_name) &&
              has(replies[1], "value=\"Office\""),
          "the setup page names the reader and lists the networks");
    check(has(replies[2], "8 to 63 characters"),
          "a password too short for WPA2 is refused at once");
    check(has(replies[3], "Joining Office"), "the choice is accepted");
    check(online, "the reader joined the chosen network");
    check(has(log.str(), "join " + first_name + " with password"),
          "the access point is named after the reader");

    // Power cycle: the same storage, a new reader and network.
    std::ostringstream again;
    bool rejoined = false;
    std::string second_name;
    {
        inkerface::network net;
        reader disciple(net, disks, view, options, again);
        second_name = disciple.name();
        disciple.start();
        rejoined = step_until(disciple, [&]() { return net.connected(); });
    }
    check(second_name == first_name, "the reader keeps its name");
    check(rejoined && has(again.str(), "joining Office") &&
              !has(again.str(), "setup"),
          "after a power cycle it joins the saved network with no setup");

    std::filesystem::remove_all(root);
}

//
// Holding the power button switches the reader off, even during the
// splash, where any other button would ask for Wi-Fi setup.
//
void test_power_off() {
    inkerface::display screen;
    inkerface::network net;
    inkerface::storage disks;
    static_cast<void>(net.connect("", ""));
    panel view(screen);

    reader_options options;
    options.port = spare_port();
    options.splash_ms = 60000;
    std::ostringstream log;
    reader disciple(net, disks, view, options, log);
    disciple.start();

    disciple.step({});
    check(!disciple.switched_off(), "a reader starts switched on");

    inkerface::input_snapshot hold;
    hold.power = true;
    disciple.step(hold);
    check(disciple.switched_off(), "a power hold switches it off");
    check(has(log.str(), "switching off") && !has(log.str(), "Wi-Fi setup"),
          "the power button is not the button that asks for setup");
    check(!view.showing_page(), "the off screen is up");

    inkerface::input_snapshot later;
    later.confirm = true;
    disciple.step(later);
    check(disciple.switched_off() && !has(log.str(), "button"),
          "once off, nothing else is acted on");
}

} // namespace

int main() {
    // A test that hangs is worse than one that fails.
    std::thread([]() {
        std::this_thread::sleep_for(60s);
        std::cout << "FAIL  the reader tests did not finish in time\n";
        std::_Exit(EXIT_FAILURE);
    }).detach();

    test_discovery_and_pages();
    test_wifi_setup();
    test_power_off();

    std::cout << "\n"
              << (failures == 0 ? "all checks passed" : "some checks failed")
              << "\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
