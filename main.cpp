#include "engine.hpp"
#include <iostream>
#include <string>

int main() {
    SimulationEngine engine;
    std::string line;
    while (std::getline(std::cin, line)) {
        json response;
        try {
            const json request = json::parse(line);
            const auto command = request.at("command").get<std::string>();
            const auto& parameters = request.at("parameters");
            if (!parameters.is_object()) {
                response = {{"status", "error"}, {"payload", {{"message", "parameters must be an object"}}}};
            } else if (command == "init") response = engine.init(parameters);
            else if (command == "tick") response = engine.tick(parameters);
            else if (command == "set_destination") response = engine.set_destination(parameters);
            else if (command == "get_state") response = engine.get_state();
            else if (command == "dock") response = engine.dock(parameters);
            else if (command == "undock") response = engine.undock(parameters);
            else if (command == "load") response = engine.load(parameters);
            else if (command == "unload") response = engine.unload(parameters);
            else if (command == "swap") response = engine.swap(parameters);
            else if (command == "get_state_at") {
                response = engine.get_state_at(parameters);
            }
            else response = {{"status", "error"}, {"payload", {{"message", "unknown command"}}}};
        } catch (const json::exception&) {
            response = {{"status", "error"}, {"payload", {{"message", "Invalid JSON request."}}}};
        }
        std::cout << response.dump() << std::endl;
        if (!std::cout) return 1;
    }
    return std::cin.bad() ? 1 : 0;
}
