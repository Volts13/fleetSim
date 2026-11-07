#include "engine.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace {
json error_response(const std::string& message) {
    return {{"status", "error"}, {"payload", {{"message", message}}}};
}

bool integer_in_range(const json& value, int minimum, int maximum) {
    return value.is_number_integer() && value >= minimum && value <= maximum;
}

std::int64_t positive_quantity(const json& value) {
    if (!value.is_number_integer())
        throw std::invalid_argument("Cargo quantity must be a positive integer.");
    if (value.is_number_unsigned() && value.get<std::uint64_t>() >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("Cargo quantity is out of range.");
    const auto quantity = value.get<std::int64_t>();
    if (quantity <= 0)
        throw std::invalid_argument("Cargo quantity must be a positive integer.");
    return quantity;
}

std::int64_t cargo_quantity(const std::vector<Cargo>& inventory, int destination) {
    for (const Cargo& cargo : inventory) {
        if (cargo.destination == destination) return cargo.quantity;
    }
    return 0;
}

void add_cargo(std::vector<Cargo>& inventory, int destination, std::int64_t quantity) {
    for (Cargo& cargo : inventory) {
        if (cargo.destination != destination) continue;
        if (cargo.quantity > std::numeric_limits<std::int64_t>::max() - quantity)
            throw std::invalid_argument("Cargo quantity is out of range.");
        cargo.quantity += quantity;
        return;
    }
    // Keep first-insertion order, as in the example output.
    inventory.push_back({destination, quantity});
}

void remove_cargo(std::vector<Cargo>& inventory, int destination) {
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->destination != destination) continue;
        if (--it->quantity == 0) inventory.erase(it);
        return;
    }
    throw std::invalid_argument("Cargo is not available.");
}

json cargo_json(const std::vector<Cargo>& inventory) {
    json result = json::array();
    for (const Cargo& cargo : inventory)
        result.push_back({{"destination", cargo.destination}, {"quantity", cargo.quantity}});
    return result;
}

// Call this on temporary docks so a bad manifest cannot partially change the world.
void apply_manifest(const json& parameters, std::vector<Dock>& target) {
    if (!parameters.is_object())
        throw std::invalid_argument("Parameters must be an object.");
    if (!parameters.contains("cargo_manifest")) return;
    const auto& manifest = parameters.at("cargo_manifest");
    if (!manifest.is_array())
        throw std::invalid_argument("cargo_manifest must be an array.");
    for (const auto& entry : manifest) {
        const int last_location = static_cast<int>(target.size()) - 1;
        if (!integer_in_range(entry.at("location"), 0, last_location) ||
            !integer_in_range(entry.at("destination"), 0, last_location))
            throw std::invalid_argument("Cargo location or destination is out of bounds.");
        const int location = entry.at("location").get<int>();
        const int destination = entry.at("destination").get<int>();
        add_cargo(target.at(location).cargo, destination, positive_quantity(entry.at("quantity")));
    }
}
}

// Initializes the simulation with the travel times matrix and starting vessel locations.
// It returns the initial state of the world at tick 0.
// Returns "Error" before initialization for other commands, or for a second init
json SimulationEngine::init(const json& parameters) {
    json response;

    // a second init returns an error
    if (is_init) {
        response["status"] = "error";
        response["payload"]["message"] = "Simulation has already been initialized.";
        return response;
    }

    response["status"] = "error";
    response["payload"]["message"] = "Invalid parameters for init.";

    try {
        // validating inputs are arrays and travel_times isn't empty and destinations are within limits
        const auto& matrix = parameters.at("travel_times");
        const auto& vessel_list = parameters.at("vessels");
        if (!matrix.is_array() || matrix.empty() || !vessel_list.is_array() ||
            matrix.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return response;
        }

        for (std::size_t row = 0; row < matrix.size(); ++row) {
            if (!matrix.at(row).is_array() || matrix.at(row).size() != matrix.size()) {
                return response;
            }
            for (std::size_t col = 0; col < matrix.size(); ++col) {
                const auto& value = matrix.at(row).at(col);
                if (!integer_in_range(value, row == col ? 0 : 1,
                                      row == col ? 0 : std::numeric_limits<int>::max())) {
                    return response;
                }
            }
        }

        // adds travel times to sim
        auto new_travel_times = matrix.get<std::vector<std::vector<int>>>();
        const auto& throughput = parameters.at("dock_throughput");
        if (!throughput.is_array() || throughput.size() != matrix.size()) return response;
        std::vector<Dock> new_docks(matrix.size());
        for (std::size_t location = 0; location < new_docks.size(); ++location) {
            if (!integer_in_range(throughput.at(location), 1, std::numeric_limits<int>::max()))
                return response;
            new_docks[location].throughput = throughput.at(location).get<int>();
            new_docks[location].remainingActions = new_docks[location].throughput;
        }
        apply_manifest(parameters, new_docks);
        std::unordered_map<std::string, Vessel> new_vessels;
        std::vector<std::string> new_initializationOrder;
        new_initializationOrder.reserve(vessel_list.size());

        // adds vessels to sim
        for (const auto& v_json : vessel_list) {
            if (!v_json.at("id").is_string() ||
                !integer_in_range(v_json.at("location"), 0,
                                  static_cast<int>(matrix.size()) - 1)) {
                return response;
            }

            Vessel v;
            v.id = v_json.at("id").get<std::string>();
            v.location = v_json.at("location").get<int>();
            v.state = "idle";
            if (new_vessels.find(v.id) != new_vessels.end()) {
                return response;
            }

            // Save it to our fast-lookup map for engine logic
            new_vessels[v.id] = v;
            new_initializationOrder.push_back(v.id);
        }

        // Save only after all parameters are valid.
        travel_times = std::move(new_travel_times);
        vessels = std::move(new_vessels);
        initializationOrder = std::move(new_initializationOrder);
        docks = std::move(new_docks);
        pendingVessels.clear();
        arrivalQueue = {};
        current_tick = 0;
        is_init = true;
    }
    catch (const json::exception&) {
        return response;
    }
    catch (const std::invalid_argument& error) {
        return error_response(error.what());
    }

    response = get_state();
    committedHistory.push_back(response.at("payload"));
    return response;
}

// Advances the simulation clock by one time unit and locks the planned state into the committed state.
// It returns the newly committed world state.
json SimulationEngine::tick(const json& parameters) {
    json response;

    // commands before init return an error
    if (!is_init) {
        response["status"] = "error";
        response["payload"]["message"] = "Simulation must be initialized first.";
        return response;
    }
    if (current_tick == std::numeric_limits<std::int64_t>::max()) {
        response["status"] = "error";
        response["payload"]["message"] = "Simulation tick limit reached.";
        return response;
    }

    // Validate all new cargo before advancing time or committing any pending changes.
    auto next_docks = docks;
    try {
        apply_manifest(parameters, next_docks);
    } catch (const json::exception&) {
        return error_response("Invalid parameters for tick.");
    } catch (const std::invalid_argument& error) {
        return error_response(error.what());
    }

    // Advance the clock
    current_tick++;

    // Commit every planned change; only actual departures enter the arrival queue.
    for (const auto& entry : pendingVessels) {
        vessels.at(entry.first) = entry.second;
        if (entry.second.state == "transit") arrivalQueue.push(entry.second);
    }
    pendingVessels.clear();

    // 2. Process vessel arrivals (Commit the state)
    while (!arrivalQueue.empty() && arrivalQueue.top().arrivalTime <= current_tick) {
        const Vessel arriving = arrivalQueue.top();
        arrivalQueue.pop();

        // Access the actual vessel data from our fast-lookup map and set it to the new location/arrived
        Vessel& v = vessels.at(arriving.id);
        v.location = arriving.destination;
        v.state = "idle";
        v.destination = -1;
        v.arrivalTime = -1;
    }

    docks = std::move(next_docks);
    for (Dock& dock : docks) dock.remainingActions = dock.throughput;

    // 3. Build the successful WorldState response payload
    response = get_state();
    committedHistory.push_back(response.at("payload"));
    return response;
}

// Queues a vessel to travel to a specified location index if it is currently idle.
// It modifies the planned state and returns an acknowledgment or error.
json SimulationEngine::set_destination(const json& parameters) {
    json response;
    // commands before init return an error
    if (!is_init) {
        response["status"] = "error";
        response["payload"]["message"] = "Simulation must be initialized first.";
        return response;
    }

    try {
        std::string v_id = parameters.at("vessel_id").get<std::string>();
        const auto& destination_value = parameters.at("destination");

        // check if vessel exists
        if (vessels.find(v_id) == vessels.end()) {
            response["status"] = "error";
            response["payload"]["message"] = "Invalid vessel ID.";
            return response;
        }

        const Vessel& v = vessels.at(v_id);

        // validate destination location
        if (!integer_in_range(destination_value, 0,
                              static_cast<int>(travel_times.size()) - 1)) {
            response["status"] = "error";
            response["payload"]["message"] = "Destination out of bounds.";
            return response;
        }
        int dest = destination_value.get<int>();

        // validate if vessel is allowed to move / be assigned a new destination
        if (v.state != "idle") {
            response["status"] = "error";
            response["payload"]["message"] = "vessel " + v.id + " is not idle.";
            return response;
        }

        // Moving to the current location will do nothing, it gets added to the queue
        // but will get popped off immediately when the tick advances

        // Calculate travel time and update the vessel's planned state
        int time_to_travel = travel_times[v.location][dest];
        if (current_tick > std::numeric_limits<std::int64_t>::max() - time_to_travel) {
            response["status"] = "error";
            response["payload"]["message"] = "Arrival tick is out of range.";
            return response;
        }

        Vessel departure = v;
        departure.state = "transit";
        departure.destination = dest;
        departure.arrivalTime = current_tick + time_to_travel;

        // Replacing this entry avoids queuing an overridden destination.
        pendingVessels[v_id] = departure;

        // Build the standard WorldState response
        return get_state();
    }
    catch (const json::exception&) {
        response["status"] = "error";
        response["payload"]["message"] = "Invalid parameters for set_destination.";
    }

    return response;
}

const Vessel& SimulationEngine::planned_vessel(const std::string& id) const {
    const auto pending = pendingVessels.find(id);
    return pending != pendingVessels.end() ? pending->second : vessels.at(id);
}

json SimulationEngine::dock(const json& parameters) {
    if (!is_init) return error_response("Simulation must be initialized first.");
    try {
        const auto id = parameters.at("vessel_id").get<std::string>();
        const auto found = vessels.find(id);
        if (found == vessels.end()) return error_response("Invalid vessel ID.");
        if (found->second.state != "idle")
            return error_response("vessel " + id + " is not idle");

        // A dock request replaces a pending trip, including its arrival time.
        Vessel planned = found->second;
        planned.state = "docked";
        planned.destination = -1;
        planned.arrivalTime = -1;
        pendingVessels[id] = std::move(planned);
        return get_state();
    } catch (const json::exception&) {
        return error_response("Invalid parameters for dock.");
    }
}

json SimulationEngine::undock(const json& parameters) {
    if (!is_init) return error_response("Simulation must be initialized first.");
    try {
        const auto id = parameters.at("vessel_id").get<std::string>();
        const auto found = vessels.find(id);
        if (found == vessels.end()) return error_response("Invalid vessel ID.");
        if (found->second.state != "docked")
            return error_response("vessel " + id + " is not docked");

        // Keep cargo changes made before this undock command.
        Vessel planned = planned_vessel(id);
        planned.state = "idle";
        pendingVessels[id] = std::move(planned);
        return get_state();
    } catch (const json::exception&) {
        return error_response("Invalid parameters for undock.");
    }
}

json SimulationEngine::load(const json& parameters) {
    return cargo_operation(parameters, true, false);
}

json SimulationEngine::unload(const json& parameters) {
    return cargo_operation(parameters, false, true);
}

json SimulationEngine::swap(const json& parameters) {
    return cargo_operation(parameters, true, true);
}

json SimulationEngine::cargo_operation(const json& parameters, bool loading, bool unloading) {
    if (!is_init) return error_response("Simulation must be initialized first.");
    try {
        const auto id = parameters.at("vessel_id").get<std::string>();
        const auto found = vessels.find(id);
        if (found == vessels.end()) return error_response("Invalid vessel ID.");
        if (found->second.state != "docked" || planned_vessel(id).state != "docked")
            return error_response("vessel " + id + " is not docked");

        const bool swapping = loading && unloading;
        int load_destination = -1;
        int unload_destination = -1;
        const int last_location = static_cast<int>(docks.size()) - 1;
        if (loading) {
            const auto& value = parameters.at(swapping ? "load_destination" : "destination");
            if (!integer_in_range(value, 0, last_location))
                return error_response("Cargo destination out of bounds.");
            load_destination = value.get<int>();
        }
        if (unloading) {
            const auto& value = parameters.at(swapping ? "unload_destination" : "destination");
            if (!integer_in_range(value, 0, last_location))
                return error_response("Cargo destination out of bounds.");
            unload_destination = value.get<int>();
        }

        // Work on copies so a failed load, unload, or swap changes nothing.
        Vessel next_vessel = planned_vessel(id);
        Dock next_dock = docks.at(next_vessel.location);
        if (loading && cargo_quantity(next_dock.cargo, load_destination) == 0)
            return error_response("Requested cargo is not available at the dock.");
        if (unloading && cargo_quantity(next_vessel.cargo, unload_destination) == 0)
            return error_response("Requested cargo is not available on the vessel.");
        if (next_dock.remainingActions == 0) {
            return error_response("transfer of " +
                std::to_string(static_cast<std::int64_t>(next_dock.throughput) + 1) +
                " operations exceeds dock throughput of " + std::to_string(next_dock.throughput) +
                " at location " + std::to_string(next_vessel.location));
        }

        // Both sources must exist before a swap. Its unload cannot supply its load.
        if (swapping && load_destination == unload_destination) {
            // Exchanging identical cargo leaves quantities unchanged, except delivery.
            if (unload_destination == next_vessel.location)
                remove_cargo(next_dock.cargo, load_destination);
        } else {
            if (loading) remove_cargo(next_dock.cargo, load_destination);
            if (unloading) remove_cargo(next_vessel.cargo, unload_destination);
            if (loading) add_cargo(next_vessel.cargo, load_destination, 1);
            if (unloading && unload_destination != next_vessel.location)
                add_cargo(next_dock.cargo, unload_destination, 1);
        }

        // A swap is one command and consumes one operation, not two.
        --next_dock.remainingActions;
        const int location = next_vessel.location;
        pendingVessels[id] = std::move(next_vessel);
        docks.at(location) = std::move(next_dock);
        return get_state();
    } catch (const json::exception&) {
        return error_response("Invalid parameters for cargo operation.");
    } catch (const std::invalid_argument& error) {
        return error_response(error.what());
    }
}

// Retrieves the current planned state of the simulation without advancing the clock.
// It reflects all commands issued since the last tick.
json SimulationEngine::get_state() {
    json response;
    if (!is_init) {
        response["status"] = "error";
        response["payload"]["message"] = "Simulation must be initialized first.";
        return response;
    }

    json vessels_obj = json::object();
    for (const std::string& v_id : initializationOrder) {
        // Show a pending departure if one exists; otherwise show committed state.
        const Vessel& v = planned_vessel(v_id);

        // Use the vessel ID as the key in the JSON object
        if (v.state != "transit") {
            vessels_obj[v_id] = {
                {"state", v.state},
                {"location", v.location}
            };
        } else {
            vessels_obj[v_id] = {
                {"state", "transit"},
                {"from", v.location},
                {"to", v.destination},
                {"arrives_at_tick", v.arrivalTime}
            };
        }
        vessels_obj[v_id]["cargo"] = cargo_json(v.cargo);
    }

    response["status"] = "ok";
    response["payload"]["tick"] = current_tick;
    response["payload"]["vessels"] = vessels_obj;
    json dock_cargo = json::array();
    for (std::size_t location = 0; location < docks.size(); ++location) {
        for (const Cargo& cargo : docks[location].cargo) {
            dock_cargo.push_back({{"location", location}, {"destination", cargo.destination},
                                  {"quantity", cargo.quantity}});
        }
    }
    response["payload"]["cargo"] = std::move(dock_cargo);
    return response;
}

json SimulationEngine::get_state_at(const json& parameters) const {
    if (!is_init) {
        return error_response("Simulation must be initialized first.");
    }

    try {
        const auto& tick_value = parameters.at("tick");
        if (!tick_value.is_number_integer()) {
            return error_response("tick must be an integer");
        }
        // Reject negative signed values before converting to an unsigned index.
        if (!tick_value.is_number_unsigned() && tick_value.get<std::int64_t>() < 0) {
            return error_response("tick must be non-negative");
        }

        const auto requested_tick = tick_value.get<std::uint64_t>();
        if (requested_tick > static_cast<std::uint64_t>(current_tick)) {
            return error_response("tick " + std::to_string(requested_tick) +
                " is in the future (current tick: " + std::to_string(current_tick) + ")");
        }

        // Returning JSON by value keeps the stored snapshot independent of the caller.
        return {{"status", "ok"},
                {"payload", committedHistory.at(static_cast<std::size_t>(requested_tick))}};
    }
    catch (const json::exception&) {
        return error_response("Invalid parameters for get_state_at.");
    }
}

