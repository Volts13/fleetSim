#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include "json.hpp"
using json = nlohmann::ordered_json;

// Cargo with the same destination shares one quantity entry.
struct Cargo {
    int destination;
    std::int64_t quantity;
};

struct Dock {
    int throughput = 0;
    int remainingActions = 0;
    std::vector<Cargo> cargo;
};

// Holds vessel data
struct Vessel {
    std::string id;
    int location;
    std::string state = "idle";
    int destination = -1;
    std::int64_t arrivalTime = -1;
    std::vector<Cargo> cargo;
};

// puts the vessel with the earliest arrival time at the top of the queue
struct EarlierArrival {
    bool operator()(const Vessel& left, const Vessel& right) const {
        return left.arrivalTime > right.arrivalTime;
    }
};

class SimulationEngine {
#ifdef FLEET_ENGINE_TESTS
    // Keep compatibility with the optional unit-test boundary accessor.
    friend struct SimulationEngineTestAccess;
#endif
private:
    // any commands before init or another init return an error
    bool is_init = false;

    // The current tick of the simulation
    std::int64_t current_tick = 0;

    // The NxN matrix storing travel times between locations
    std::vector<std::vector<int>> travel_times;

    // A map for fast lookup of vessels by their string ID (committed state)
    std::unordered_map<std::string, Vessel> vessels;

    // The latest planned changes, including movement, docking, and cargo.
    std::unordered_map<std::string, Vessel> pendingVessels;

    // docks[location] holds planned cargo and the shared remaining tick budget.
    std::vector<Dock> docks;

    // committed trips, with the earliest arrival at the top
    std::priority_queue<Vessel, std::vector<Vessel>, EarlierArrival> arrivalQueue;

    // vessel IDs in the order they were supplied to init
    std::vector<std::string> initializationOrder;

    // Committed world-state payloads; each vector index is its tick number.
    std::vector<json> committedHistory;

    const Vessel& planned_vessel(const std::string& id) const;
    json cargo_operation(const json& parameters, bool loading, bool unloading);

public:
    // Initializes the simulation with the travel times matrix and starting vessel locations.
    // It returns the initial state of the world at tick 0.
    // Returns "Error" before initialization for other commands, or for a second init
    json init(const json& parameters);

    // Advances the simulation clock by one time unit and locks the planned state into the committed state.
    // It returns the newly committed world state.
    json tick(const json& parameters = json::object());

    // Queues a vessel to travel to a specified location index if it is currently idle.
    // It modifies the planned state and returns an acknowledgment or error.
    json set_destination(const json& parameters);

    // Docking eligibility is checked against the last committed vessel state.
    json dock(const json& parameters);
    json undock(const json& parameters);

    // Each successful command consumes one operation at the vessel's dock.
    json load(const json& parameters);
    json unload(const json& parameters);
    json swap(const json& parameters);

    // Retrieves the current planned state of the simulation without advancing the clock.
    // It reflects all commands issued since the last tick.
    json get_state();

    // Read a committed snapshot without exposing pending changes or advancing time.
    json get_state_at(const json& parameters) const;
};

#endif // ENGINE_HPP
