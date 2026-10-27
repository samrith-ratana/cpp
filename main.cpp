#include <httplib.h>
#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    // Vercel passes the PORT environment variable, defaults to 80
    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 80;

    httplib::Server svr;

    // Root route
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Hello from C++ on Vercel! 🚀", "text/plain");
    });

    // Health check
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    std::cout << "C++ server listening on port " << port << std::endl;
    svr.listen("0.0.0.0", port);
    return 0;
}