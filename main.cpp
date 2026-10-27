
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;

// =======================================
// TELEGRAM CONFIGURATION
// =======================================
const std::string TELEGRAM_BOT_TOKEN = "7224164543:AAE1QlB8Al6m_QULBWKVvVqvMpo07aN1z0k";
const std::string TELEGRAM_CHAT_ID = "5580962565";

// Escape Telegram HTML characters
std::string escape_html(const std::string& text) {
    std::string result;

    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            default: result += c;
        }
    }

    return result;
}

// Read JSON string field safely
std::string get_field(const json& data, const char* key) {
    auto it = data.find(key);

    if (it != data.end() && it->is_string()) {
        return it->get<std::string>();
    }

    return "";
}

// Send message to Telegram
bool sendTelegramMessage(const std::string& message) {

    httplib::SSLClient client("api.telegram.org", 443);

    client.set_ca_cert_path(
        "/etc/ssl/certs/ca-certificates.crt"
    );

    client.enable_server_certificate_verification(true);
    client.enable_server_hostname_verification(true);
    client.set_connection_timeout(5);
    client.set_read_timeout(12);
    client.set_write_timeout(5);

    json payload = {
        {"chat_id", TELEGRAM_CHAT_ID},
        {"text", message},
        {"parse_mode", "HTML"}
    };

    auto response = client.Post(
        "/bot" + TELEGRAM_BOT_TOKEN + "/sendMessage",
        payload.dump(),
        "application/json"
    );

    if (!response || response->status != 200) {
        std::cerr << "Telegram request failed\n";
        return false;
    }

    json result = json::parse(
        response->body, nullptr, false
    );

    return result.is_object() &&
           result.value("ok", false);
}

int main() {
    const char* port_env = std::getenv("PORT");
    int port = 80;

    if (port_env) {
        try {
            port = std::stoi(port_env);

            if (port < 1 || port > 65535) {
                throw std::out_of_range("Invalid port");
            }
        } catch (const std::exception&) {
            std::cerr << "Invalid PORT\n";
            return 1;
        }
    }

    httplib::Server svr;
    svr.set_payload_max_length(8192);

    // Root - Serve contact form
    svr.Get("/", [](const httplib::Request&,
                    httplib::Response& res) {

        std::ifstream file("public/index.html");

        if (!file) {
            res.status = 404;
            res.set_content("Form not found", "text/plain");
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        res.set_content(
            buffer.str(),
            "text/html; charset=utf-8"
        );
    });

    // Health check
    svr.Get("/health", [](const httplib::Request&,
                          httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    // Test Telegram notification
    // Local testing only; do not expose publicly.
    svr.Get("/test-telegram",
        [](const httplib::Request&,
           httplib::Response& res) {

        res.status = 404;
        res.set_content("Not Found", "text/plain");
    });

    // Customer Contact Form API
    svr.Post("/api/contact",
        [](const httplib::Request& req,
           httplib::Response& res) {

        if (req.get_header_value("Content-Type")
                .rfind("application/json", 0) != 0) {
            res.status = 415;
            res.set_content(
                R"({"success":false,"message":"JSON required"})",
                "application/json"
            );
            return;
        }

        json data = json::parse(
            req.body, nullptr, false
        );

        if (!data.is_object()) {
            res.status = 400;
            res.set_content(
                R"({"success":false,"message":"Invalid JSON"})",
                "application/json"
            );
            return;
        }

        std::string name = get_field(data, "name");
        std::string phone = get_field(data, "phone");
        std::string service = get_field(data, "service");
        std::string message = get_field(data, "message");

        // Honeypot spam protection
        if (!get_field(data, "website").empty()) {
            res.set_content(
                R"({"success":true,"message":"Received"})",
                "application/json"
            );
            return;
        }

        if (
            name.empty() || name.size() > 100 ||
            phone.size() < 6 || phone.size() > 30 ||
            service.empty() || service.size() > 100 ||
            message.empty() || message.size() > 1000
        ) {
            res.status = 400;
            res.set_content(
                R"({"success":false,"message":"Invalid fields"})",
                "application/json"
            );
            return;
        }

        std::string text =
            "📩 <b>NEW CUSTOMER REQUEST</b>\n\n"
            "👤 <b>Name:</b> " + escape_html(name) + "\n"
            "📞 <b>Phone:</b> " + escape_html(phone) + "\n"
            "🛠 <b>Service:</b> " + escape_html(service) + "\n\n"
            "💬 <b>Message:</b>\n" + escape_html(message);

        bool sent = sendTelegramMessage(text);

        if (sent) {
            res.set_content(
                R"({"success":true,"message":"Request sent successfully"})",
                "application/json"
            );
        } else {
            res.status = 502;
            res.set_content(
                R"({"success":false,"message":"Failed to send Telegram message"})",
                "application/json"
            );
        }
    });

    std::cout << "Server running on port "
              << port << std::endl;

    if (!svr.listen("0.0.0.0", port)) {
        return 1;
    }

    return 0;
}
