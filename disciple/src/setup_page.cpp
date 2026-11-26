//
// The pages a phone sees during Wi-Fi setup.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/setup_page.h"

#include "disciple/portal.h"
#include "disciple/version.h"

namespace disciple {
namespace {

// Shared by both pages. Light by default, dark when the phone is.
constexpr const char *style = R"css(
:root{--bg:#f6f4ef;--card:#fff;--ink:#1d1b17;--soft:#6b665c;
--line:#e3dfd5;--accent:#2f5d50;--accent-ink:#fff;--bad:#9b2c1f;
--bad-bg:#fbe9e5}
@media (prefers-color-scheme:dark){:root{--bg:#171613;--card:#211f1b;
--ink:#f1eee6;--soft:#a39d90;--line:#36332c;--accent:#7fb8a4;
--accent-ink:#10201b;--bad:#f0a497;--bad-bg:#3a1f1a}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
font:16px/1.45 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
main{max-width:440px;margin:0 auto;padding:24px 16px 40px}
h1{font-size:22px;margin:0 0 4px}
.sub{color:var(--soft);margin:0 0 20px}
.card{background:var(--card);border:1px solid var(--line);
border-radius:14px;overflow:hidden;margin-bottom:16px}
.net{display:flex;align-items:center;gap:12px;padding:14px 16px;
border-top:1px solid var(--line);cursor:pointer}
.net:first-child{border-top:0}
.net input{accent-color:var(--accent);width:20px;height:20px;margin:0}
.name{flex:1;overflow-wrap:anywhere}
.lock{color:var(--soft);font-size:13px}
.bars{display:flex;align-items:flex-end;gap:2px;height:16px}
.bars i{width:4px;background:var(--line);border-radius:1px}
.bars i.on{background:var(--ink)}
.other{width:100%;margin-top:8px}
label.field{display:block;font-weight:600;margin:0 0 6px}
input[type=text],input[type=password]{width:100%;font:inherit;
padding:12px;border:1px solid var(--line);border-radius:10px;
background:var(--card);color:var(--ink)}
.show{display:flex;gap:8px;align-items:center;color:var(--soft);
font-size:14px;margin:8px 0 20px}
button{width:100%;font:inherit;font-weight:600;padding:14px;border:0;
border-radius:12px;background:var(--accent);color:var(--accent-ink)}
.problem{background:var(--bad-bg);color:var(--bad);padding:12px 14px;
border-radius:10px;margin-bottom:16px}
.rescan{display:block;text-align:center;margin-top:14px;
color:var(--accent)}
footer{color:var(--soft);font-size:13px;text-align:center;
margin-top:28px}
)css";

std::string head(const std::string &title) {
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,"
           "initial-scale=1\"><title>" +
           html_escape(title) + "</title><style>" + style +
           "</style></head><body><main>";
}

std::string foot() {
    return std::string("<footer>tabcast disciple ") + version +
           "</footer></main></body></html>";
}

//
// Four bars, as many lit as the signal deserves.
//
std::string bars(int dbm) {
    const int lit = dbm >= -55 ? 4 : dbm >= -67 ? 3 : dbm >= -75 ? 2 : 1;
    std::string out = "<span class=\"bars\" aria-label=\"signal " +
                      std::to_string(lit) + " of 4\">";
    for (int bar = 1; bar <= 4; ++bar) {
        out += "<i class=\"" + std::string(bar <= lit ? "on" : "") +
               "\" style=\"height:" + std::to_string(4 * bar) +
               "px\"></i>";
    }
    return out + "</span>";
}

} // namespace

std::string setup_page_html(const setup_form &form) {
    std::string page = head("Set up " + form.reader);

    page += "<h1>Connect this reader to Wi-Fi</h1>";
    page += "<p class=\"sub\">" + html_escape(form.reader) +
            " will join the network you choose, then find the "
            "broadcast by itself.</p>";

    if (!form.problem.empty()) {
        page += "<div class=\"problem\" role=\"alert\">" +
                html_escape(form.problem) + "</div>";
    }

    page += "<form method=\"post\" action=\"/join\"><div class=\"card\">";

    bool any_chosen = false;
    for (const inkerface::wifi_network &network : form.networks) {
        const bool chosen = network.ssid == form.chosen;
        any_chosen = any_chosen || chosen;
        page += "<label class=\"net\"><input type=\"radio\" "
                "name=\"network\" value=\"" +
                html_escape(network.ssid) + "\"" +
                (chosen ? " checked" : "") + "><span class=\"name\">" +
                html_escape(network.ssid) + "</span>" +
                (network.secured ? "<span class=\"lock\">locked</span>"
                                 : "") +
                bars(network.rssi) + "</label>";
    }

    // A network that hides its name, or was not found this time.
    const bool other = !form.chosen.empty() && !any_chosen;
    page += std::string("<label class=\"net\"><input type=\"radio\" "
                        "name=\"network\" value=\"\"") +
            (other || form.networks.empty() ? " checked" : "") +
            "><span class=\"name\">Other network<input class=\"other\" "
            "type=\"text\" name=\"other\" maxlength=\"32\" "
            "autocapitalize=\"none\" autocorrect=\"off\" "
            "placeholder=\"Network name\" value=\"" +
            html_escape(other ? form.chosen : "") +
            "\" onfocus=\"this.closest('label').querySelector("
            "'input[type=radio]').checked=true\"></span></label>";

    page += "</div>";

    page += "<label class=\"field\" for=\"password\">Password</label>"
            "<input type=\"password\" id=\"password\" name=\"password\" "
            "maxlength=\"63\" autocapitalize=\"none\" autocorrect=\"off\" "
            "autocomplete=\"off\">"
            "<label class=\"show\"><input type=\"checkbox\" "
            "onchange=\"document.getElementById('password').type="
            "this.checked?'text':'password'\">Show password</label>"
            "<button type=\"submit\">Join network</button></form>"
            "<a class=\"rescan\" href=\"/?rescan=1\">Look for networks "
            "again</a>";

    return page + foot();
}

std::string joining_page_html(const std::string &reader,
                              const std::string &ssid) {
    std::string page = head(reader + " is joining");
    page += "<h1>Joining " + html_escape(ssid) + "</h1>";
    page += "<p class=\"sub\">" + html_escape(reader) +
            " is leaving its setup network to join " + html_escape(ssid) +
            ", so this phone will lose its connection to it. That is "
            "expected.</p>";
    page += "<div class=\"card\"><p class=\"net\">Watch the reader's "
            "screen. Once it says <b>Connecting...</b> and then shows a "
            "page, you are done.</p><p class=\"net\">If the password was "
            "wrong, the reader comes back as " +
            html_escape(reader) +
            " within a minute. Join it again and this page will say what "
            "went wrong.</p></div>";
    return page + foot();
}

} // namespace disciple
