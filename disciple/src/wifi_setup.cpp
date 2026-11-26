//
// Getting a reader onto Wi-Fi, with a phone as its keyboard.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/wifi_setup.h"

#include "disciple/setup_page.h"

#include <algorithm>
#include <optional>
#include <ostream>

namespace disciple {
namespace {

using namespace std::chrono_literals;

// How long to give a network to accept the reader.
constexpr auto join_timeout = 20s;

// How long to let the phone's browser fetch the "joining" page before
// the access point it is on goes away.
constexpr auto leave_delay = 1500ms;

// While in setup with a network saved, try it again this often, if the
// setup page has been left alone this long.
constexpr auto idle_retry = 120s;

// If the access point will not start, try again after this long.
constexpr auto setup_retry = 10s;

// Where the setup page and DNS are served. A desktop's emulated access
// point is on someone else's network, where port 80 needs root and DNS
// is not the reader's to answer.
constexpr int portal_port = 80;
constexpr int emulated_portal_port = 8080;
constexpr int dns_port = 53;

// WPA2 limits, which the page checks so the reader does not spend twenty
// seconds discovering what the form could have said at once.
constexpr std::size_t ssid_longest = 32;
constexpr std::size_t password_shortest = 8;
constexpr std::size_t password_longest = 63;

//
// One entry per network name, the strongest, strongest first.
//
std::vector<inkerface::wifi_network>
tidy(std::vector<inkerface::wifi_network> found) {
    std::sort(found.begin(), found.end(),
              [](const inkerface::wifi_network &a,
                 const inkerface::wifi_network &b) { return a.rssi > b.rssi; });
    std::vector<inkerface::wifi_network> out;
    for (const inkerface::wifi_network &network : found) {
        if (network.ssid.empty()) {
            continue;
        }
        const bool seen = std::any_of(
            out.begin(), out.end(), [&](const inkerface::wifi_network &kept) {
                return kept.ssid == network.ssid;
            });
        if (!seen) {
            out.push_back(network);
        }
    }
    return out;
}

} // namespace

wifi_setup::wifi_setup(inkerface::network &net, inkerface::storage &disks,
                       panel &view, timer &clock, std::ostream &log, int port)
    : _net(net)
    , _disks(disks)
    , _view(view)
    , _clock(clock)
    , _log(log)
    , _port(port) {
    settings_load(_disks, _saved);
    if (_saved.name.empty()) {
        // First power-on. The name is chosen now and kept, so the
        // network a phone is told to look for never changes.
        _saved.name = new_reader_name();
        std::string error;
        if (!settings_save(_disks, _saved, error)) {
            note("warning", "cannot save settings: " + error);
        }
    }
}

wifi_setup::~wifi_setup() {
    if (_in_setup) {
        _net.stop_access_point();
    }
}

const std::string &wifi_setup::reader_name() const {
    return _saved.name;
}

bool wifi_setup::in_setup() const {
    return _in_setup;
}

void wifi_setup::start(bool force_setup, std::function<void()> on_online) {
    _on_online = std::move(on_online);
    if (force_setup || !_saved.has_network()) {
        enter_setup("");
        return;
    }
    join(_saved.ssid, _saved.password);
}

void wifi_setup::poll() {
    _portal.poll();
    _dns.poll();
}

//
// Join a network, and go online or back to setup.
//
// Notes:
//      Blocks for up to join_timeout. The panel says "Connecting..."
//      first, and there is nothing else for a reader to do meanwhile.
//
void wifi_setup::join(const std::string &ssid, const std::string &password) {
    _trying_ssid = ssid;
    note("wifi", "joining " + ssid);
    _view.status("Connecting...", "joining " + ssid);

    if (!_net.connect(ssid, password, join_timeout)) {
        // The radio does not say which; these are the usual two.
        const std::string reason = "wrong password, or out of range";
        note("wifi", "could not join " + ssid + ": " + reason);
        enter_setup("Could not join " + ssid + ": " + reason);
        return;
    }

    note("wifi", "joined " + ssid);
    if (_saved.ssid != ssid || _saved.password != password) {
        _saved.ssid = ssid;
        _saved.password = password;
        std::string error;
        if (!settings_save(_disks, _saved, error)) {
            note("warning", "cannot save settings: " + error);
        }
    }
    const std::function<void()> next = std::move(_on_online);
    _on_online = nullptr;
    next();
}

//
// Become the reader's own access point and serve the setup page.
//
void wifi_setup::enter_setup(const std::string &problem) {
    _problem = problem;
    if (_ap_password.empty()) {
        _ap_password = new_access_point_password();
    }

    // Scanned before the access point starts, while the radio is free
    // to listen on every channel.
    rescan();

    const std::optional<inkerface::access_point> hosted =
        _net.start_access_point(_saved.name, _ap_password);
    if (!hosted) {
        note("wifi", "cannot start the access point");
        _view.status("Wi-Fi setup failed", "the radio would not start");
        _clock.set(setup_retry, [this, problem]() { enter_setup(problem); });
        return;
    }
    _in_setup = true;
    _emulated = hosted->emulated;
    _address = hosted->address;
    _last_visit = clock_type::now();

    std::string error;
    if (!_portal.is_open() &&
        !_portal.open(
            _net, page_port(),
            [this](const http_request &request) { return answer(request); },
            error)) {
        note("warning", error);
    }
    if (!_emulated && !_dns.is_open() &&
        !_dns.open(_net, dns_port, _address, error)) {
        // Not fatal: the phone just will not open the page by itself, and
        // the panel gives the address to type.
        note("warning", error);
    }

    note("setup", "join " + _saved.name + " with password " + _ap_password +
                      ", then open " + page_address());
    std::string shown = page_address();
    shown = shown.substr(shown.find("//") + 2);
    shown.pop_back();
    _view.setup(_saved.name, _ap_password, shown, _problem);

    if (_saved.has_network()) {
        _clock.set(idle_retry, [this]() { retry_saved_when_idle(); });
    } else {
        _clock.cancel();
    }
}

void wifi_setup::leave_setup() {
    _portal.close();
    _dns.close();
    if (_in_setup) {
        _net.stop_access_point();
        _in_setup = false;
    }
}

//
// Try the saved network again, unless someone is using the page.
//
void wifi_setup::retry_saved_when_idle() {
    if (clock_type::now() - _last_visit < idle_retry) {
        _clock.set(idle_retry, [this]() { retry_saved_when_idle(); });
        return;
    }
    note("wifi", "nobody is using setup, trying " + _saved.ssid + " again");
    leave_setup();
    join(_saved.ssid, _saved.password);
}

void wifi_setup::rescan() {
    _networks = tidy(_net.scan());
}

//
// The address of the setup page, as a phone would type it.
//
std::string wifi_setup::page_address() const {
    const int port = page_port();
    return "http://" + _address +
           (port == 80 ? std::string() : ":" + std::to_string(port)) + "/";
}

//
// The port the setup page is served on.
//
int wifi_setup::page_port() const {
    if (_port != 0) {
        return _port;
    }
    return _emulated ? emulated_portal_port : portal_port;
}

//
// Answer one request from the phone.
//
http_reply wifi_setup::answer(const http_request &request) {
    _last_visit = clock_type::now();

    const int port = page_port();
    const bool ours = request.host == _address ||
                      request.host == _address + ":" + std::to_string(port);

    http_reply reply;

    // A phone checking for the internet asks for some other site's page.
    // Sending it here instead is what makes it show the setup.
    if (!ours) {
        reply.status = 302;
        reply.location = page_address();
        return reply;
    }
    if (request.method == "POST" && request.path == "/join") {
        return take_form(request);
    }
    if (request.method == "GET" && request.path == "/") {
        if (request.query.find("rescan=1") != std::string::npos) {
            rescan();
        }
        setup_form form;
        form.reader = _saved.name;
        form.networks = _networks;
        form.chosen = _trying_ssid.empty() ? _saved.ssid : _trying_ssid;
        form.problem = _problem;
        reply.body = setup_page_html(form);
        return reply;
    }
    if (request.path == "/favicon.ico") {
        reply.status = 404;
        reply.content_type = "text/plain";
        return reply;
    }
    reply.status = 302;
    reply.location = page_address();
    return reply;
}

//
// The phone chose a network.
//
http_reply wifi_setup::take_form(const http_request &request) {
    std::string ssid = form_value(request.body, "network");
    if (ssid.empty()) {
        ssid = form_value(request.body, "other");
    }
    std::string password = form_value(request.body, "password");

    const auto known = std::find_if(
        _networks.begin(), _networks.end(),
        [&](const inkerface::wifi_network &network) {
            return network.ssid == ssid;
        });
    const bool secured = known == _networks.end() || known->secured;

    std::string refusal;
    if (ssid.empty()) {
        refusal = "Choose a network, or type the name of one.";
    } else if (ssid.size() > ssid_longest) {
        refusal = "A network name is at most 32 characters.";
    } else if (secured && (password.size() < password_shortest ||
                           password.size() > password_longest)) {
        refusal = "A Wi-Fi password is 8 to 63 characters.";
    }

    http_reply reply;
    if (!refusal.empty()) {
        setup_form form;
        form.reader = _saved.name;
        form.networks = _networks;
        form.chosen = ssid;
        form.problem = refusal;
        reply.body = setup_page_html(form);
        return reply;
    }

    if (!secured) {
        password.clear();
    }
    note("setup", "a phone chose " + ssid);

    // The reply has to reach the phone before the access point it is on
    // disappears, so leaving waits a moment.
    _trying_ssid = ssid;
    _clock.set(leave_delay, [this, ssid, password]() {
        leave_setup();
        join(ssid, password);
    });

    reply.body = joining_page_html(_saved.name, ssid);
    return reply;
}

//
// Report one event, in the same columns as the rest of the reader.
//
void wifi_setup::note(const char *label, const std::string &text) {
    std::string padded(label);
    padded.resize(std::max<std::size_t>(padded.size(), 8), ' ');
    _log << "  " << padded << " " << text << std::endl;
}

} // namespace disciple
