//
// Tests for the parts of Wi-Fi setup that are pure logic: the form and
// HTML helpers, the captive DNS answer, the setup page, the settings
// file format, and the names and passwords a reader makes up.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/portal.h"
#include "disciple/settings.h"
#include "disciple/setup_page.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace {

using namespace disciple;

int failures = 0;

void check(bool passed, const std::string &what) {
    std::cout << (passed ? "PASS  " : "FAIL  ") << what << "\n";
    if (!passed) {
        ++failures;
    }
}

bool has(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

void test_helpers() {
    check(form_value("network=Parish+Office&password=a%26b%3Dc",
                     "network") == "Parish Office",
          "a form value decodes '+' as a space");
    check(form_value("network=x&password=a%26b%3Dc", "password") == "a&b=c",
          "a form value decodes %XX");
    check(form_value("network=x", "password").empty(),
          "an absent form field is empty");
    check(form_value("p=%zz%4", "p") == "%zz%4",
          "a malformed escape is kept as it was typed");
    check(html_escape("<a href=\"x\">&'") ==
              "&lt;a href=&quot;x&quot;&gt;&amp;&#39;",
          "HTML escaping covers attributes too");
}

void test_names() {
    check(std::regex_match(new_reader_name(),
                           std::regex("sheep_[0-9a-f]{8}")),
          "a reader is named sheep_ and eight hex digits");
    check(new_reader_name() != new_reader_name(),
          "two readers get different names");
    check(std::regex_match(new_access_point_password(),
                           std::regex("[a-hj-km-np-z2-9]{8}")),
          "the access point password has no look-alike characters");
}

void test_settings() {
    reader_settings saved;
    saved.ssid = "Parish \"Office\"";
    saved.password = "a\\b";
    saved.name = "sheep_0badf00d";
    reader_settings back;
    check(settings_from_json(settings_to_json(saved), back) &&
              back.ssid == saved.ssid && back.password == saved.password &&
              back.name == saved.name,
          "settings survive the round trip, quotes and all");
    check(!settings_from_json("{}", back), "empty settings are not settings");

    reader_settings named;
    named.name = "sheep_12345678";
    check(settings_from_json(settings_to_json(named), back) &&
              !back.has_network(),
          "a name with no network yet is still worth keeping");
}

void test_dns() {
    // A query for example.com, type A, class IN, id 0x1234.
    std::vector<std::uint8_t> query = {0x12, 0x34, 0x01, 0x00, 0, 1,
                                       0,    0,    0,    0,    0, 0};
    for (const char *label : {"example", "com"}) {
        query.push_back(static_cast<std::uint8_t>(std::strlen(label)));
        query.insert(query.end(), label, label + std::strlen(label));
    }
    query.push_back(0);
    query.insert(query.end(), {0, 1, 0, 1});

    // 192.168.4.1, as the four bytes in memory order.
    const std::uint8_t bytes[4] = {192, 168, 4, 1};
    std::uint32_t address = 0;
    std::memcpy(&address, bytes, sizeof(address));

    const std::vector<std::uint8_t> answer =
        dns_answer(query.data(), query.size(), address);
    check(answer.size() == query.size() + 16,
          "an A question gets one sixteen byte answer");
    check(answer.size() >= 8 && answer[0] == 0x12 && answer[1] == 0x34 &&
              (answer[2] & 0x80) != 0 && answer[7] == 1,
          "the answer carries the question's id, is a response, has one record");
    check(answer.size() == query.size() + 16 &&
              answer[answer.size() - 4] == 192 &&
              answer[answer.size() - 1] == 1,
          "every name is the reader");

    std::vector<std::uint8_t> aaaa = query;
    aaaa[aaaa.size() - 3] = 28;
    const std::vector<std::uint8_t> none =
        dns_answer(aaaa.data(), aaaa.size(), address);
    check(none.size() == aaaa.size() && none[7] == 0,
          "an IPv6 question is answered with no records");

    std::vector<std::uint8_t> response = query;
    response[2] |= 0x80;
    check(dns_answer(response.data(), response.size(), address).empty(),
          "a response is never answered");
    check(dns_answer(query.data(), 15, address).empty(),
          "a truncated question is dropped");
}

void test_page() {
    setup_form form;
    form.reader = "sheep_0badf00d";
    form.networks = {{"Parish Office", -50, true}, {"Hall <Guest>", -70, false}};
    form.chosen = "Parish Office";
    form.problem = "Could not join";
    const std::string page = setup_page_html(form);

    check(has(page, "value=\"Parish Office\" checked"),
          "the network last tried is selected");
    check(has(page, "Hall &lt;Guest&gt;") && !has(page, "<Guest>"),
          "network names are escaped");
    check(has(page, "Could not join"), "the problem is shown");
    check(has(page, "action=\"/join\""), "the form posts to /join");
    check(has(joining_page_html("sheep_0badf00d", "Parish Office"),
              "Joining Parish Office"),
          "the joining page names the network");
}

} // namespace

int main() {
    test_helpers();
    test_names();
    test_settings();
    test_dns();
    test_page();

    std::cout << "\n"
              << (failures == 0 ? "all checks passed" : "some checks failed")
              << "\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
