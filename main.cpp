
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

// Read a configuration value from the environment.
static std::string env_or_empty(const char* key) {
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string();
}

static std::string trim(std::string value) {
    auto not_space = [](unsigned char c) {
        return !std::isspace(c);
    };

    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), not_space)
    );

    value.erase(
        std::find_if(value.rbegin(), value.rend(), not_space).base(),
        value.end()
    );

    return value;
}

static std::string get_field(
    const json& data,
    const char* key
) {
    auto it = data.find(key);

    if (it == data.end() || !it->is_string()) {
        return "";
    }

    return trim(it->get<std::string>());
}

// Escape values before using Telegram HTML formatting.
static std::string escape_html(const std::string& text) {
    std::string result;

    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default:  result += c; break;
        }
    }

    return result;
}

static bool valid_phone(const std::string& phone) {
    if (phone.size() < 6 || phone.size() > 30) {
        return false;
    }

    int digits = 0;

    for (unsigned char c : phone) {
        if (std::isdigit(c)) {
            ++digits;
        } else if (
            c != '+' && c != '-' &&
            c != '(' && c != ')' && c != ' '
        ) {
            return false;
        }
    }

    return digits >= 6 && digits <= 16;
}

static void respond(
    httplib::Response& res,
    int status,
    bool success,
    const std::string& message
) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");

    res.set_content(
        json{
            {"success", success},
            {"message", message}
        }.dump(),
        "application/json; charset=utf-8"
    );
}

// Send an outgoing HTTPS message to Telegram.
// No Telegram webhook is required.
static bool send_telegram(
    const std::string& token,
    const std::string& chat_id,
    const std::string& message
) {
    httplib::SSLClient client("api.telegram.org", 443);

    client.set_ca_cert_path(
        "/etc/ssl/certs/ca-certificates.crt"
    );

    client.enable_server_certificate_verification(true);
    client.enable_server_hostname_verification(true);

    client.set_connection_timeout(5);
    client.set_read_timeout(12);
    client.set_write_timeout(5);

    json body = {
        {"chat_id", chat_id},
        {"text", message},
        {"parse_mode", "HTML"}
    };

    auto result = client.Post(
        "/bot" + token + "/sendMessage",
        body.dump(),
        "application/json"
    );

    if (!result || result->status != 200) {
        return false;
    }

    json response = json::parse(
        result->body, nullptr, false
    );

    return response.is_object() &&
           response.value("ok", false);
}

int main() {
    const std::string token =
        env_or_empty("TELEGRAM_BOT_TOKEN");

    const std::string chat_id =
        env_or_empty("TELEGRAM_CHAT_ID");

    const std::string allowed_origin =
        env_or_empty("ALLOWED_ORIGIN");

    if (token.empty() || chat_id.empty()) {
        std::cerr << "Missing Telegram configuration\n";
        return 1;
    }

    int port = 80;

    const std::string port_env = env_or_empty("PORT");

    if (!port_env.empty()) {
        try {
            size_t pos = 0;
            int parsed = std::stoi(port_env, &pos);

            if (pos != port_env.size() ||
                parsed < 1 || parsed > 65535) {
                throw std::out_of_range("Invalid port");
            }

            port = parsed;
        } catch (const std::exception&) {
            std::cerr << "Invalid PORT\n";
            return 1;
        }
    }

    httplib::Server svr;

    svr.set_payload_max_length(8 * 1024);
    svr.set_read_timeout(10);
    svr.set_write_timeout(15);

    // Serve the customer form.
    svr.Get("/", [](const httplib::Request&,
                    httplib::Response& res) {

        std::ifstream file(
            "public/index.html",
            std::ios::binary
        );

        if (!file) {
            res.status = 500;
            res.set_content(
                "Form not found",
                "text/plain"
            );
            return;
        }

        std::ostringstream content;
        content << file.rdbuf();

        res.set_content(
            content.str(),
            "text/html; charset=utf-8"
        );
    });

    // Health check.
    svr.Get("/health", [](const httplib::Request&,
                          httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    // Optional CORS support for a separate website.
    auto cors = [allowed_origin](
        const httplib::Request& req,
        httplib::Response& res
    ) {
        if (!allowed_origin.empty() &&
            req.get_header_value("Origin") == allowed_origin) {

            res.set_header(
                "Access-Control-Allow-Origin",
                allowed_origin
            );
            res.set_header("Vary", "Origin");
        }
    };

    svr.Options("/api/contact",
        [cors, allowed_origin](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            if (allowed_origin.empty() ||
                req.get_header_value("Origin") != allowed_origin) {
                res.status = 403;
                return;
            }

            cors(req, res);

            res.set_header(
                "Access-Control-Allow-Methods",
                "POST, OPTIONS"
            );

            res.set_header(
                "Access-Control-Allow-Headers",
                "Content-Type"
            );

            res.status = 204;
        }
    );

    // Customer submits form -> Telegram notification.
    svr.Post("/api/contact",
        [token, chat_id, cors](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            cors(req, res);

            if (req.get_header_value("Content-Type")
                    .rfind("application/json", 0) != 0) {
                respond(res, 415, false, "Use application/json.");
                return;
            }

            json data = json::parse(
                req.body, nullptr, false
            );

            if (!data.is_object()) {
                respond(res, 400, false, "Invalid JSON.");
                return;
            }

            // Hidden spam-trap field.
            if (!get_field(data, "website").empty()) {
                respond(res, 200, true, "Request received.");
                return;
            }

            std::string name = get_field(data, "name");
            std::string phone = get_field(data, "phone");
            std::string service = get_field(data, "service");
            std::string message = get_field(data, "message");

            if (
                name.size() < 2 || name.size() > 200 ||
                !valid_phone(phone) ||
                service.empty() || service.size() > 100 ||
                message.empty() || message.size() > 3000
            ) {
                respond(
                    res, 400, false,
                    "Please check your form fields."
                );
                return;
            }

            std::string notification =
                "📩 <b>New Customer Request</b>\n\n"
                "👤 <b>Name:</b> " + escape_html(name) + "\n"
                "📞 <b>Phone:</b> " + escape_html(phone) + "\n"
                "🛠 <b>Service:</b> " + escape_html(service) + "\n"
                "💬 <b>Message:</b>\n" + escape_html(message);

            bool sent = send_telegram(
                token,
                chat_id,
                notification
            );

            if (!sent) {
                std::cerr << "Telegram delivery failed\n";

                respond(
                    res, 502, false,
                    "Could not send. Please try again."
                );
                return;
            }

            respond(
                res, 200, true,
                "Thank you! We will contact you soon."
            );
        }
    );

    std::cout << "Server listening on port "
              << port << std::endl;

    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Server failed to start\n";
        return 1;
    }

    return 0;
}
