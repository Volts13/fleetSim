#pragma once
#ifndef FLEET_ENGINE_TESTS
#error Compile both the tests and engine.cpp with -DFLEET_ENGINE_TESTS
#endif
#include "engine.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

// Test-only access; ordinary fixtures still use real commands.
struct SimulationEngineTestAccess {
    static void clock(SimulationEngine& e, std::int64_t tick) { e.current_tick = tick; }
    static void cargo(SimulationEngine& e, const std::vector<Cargo>& cargo) {
        e.vessels.at("v1").cargo = cargo;
        e.pendingVessels.erase("v1");
    }
    static void dock_cargo(SimulationEngine& e, const std::vector<Cargo>& cargo) {
        e.docks.at(0).cargo = cargo;
    }
    static json snapshot(const SimulationEngine& e) {
        auto pack = [](const Vessel& v) {
            json cargo = json::array();
            for (const auto& c : v.cargo) cargo.push_back({c.destination, c.quantity});
            return json::array({v.id, v.location, v.state, v.destination, v.arrivalTime, cargo});
        };
        json committed = json::object(), pending = json::object(), docks = json::array();
        // Sort map keys only for this private-state comparison.
        for (const auto& id : e.initializationOrder) committed[id] = pack(e.vessels.at(id));
        for (const auto& id : e.initializationOrder) {
            auto it = e.pendingVessels.find(id);
            if (it != e.pendingVessels.end()) pending[id] = pack(it->second);
        }
        for (const auto& d : e.docks) {
            json cargo = json::array();
            for (const auto& c : d.cargo) cargo.push_back({c.destination, c.quantity});
            docks.push_back({d.throughput, d.remainingActions, cargo});
        }
        auto queue = e.arrivalQueue;
        json arrivals = json::array();
        while (!queue.empty()) { arrivals.push_back(pack(queue.top())); queue.pop(); }
        return {{"initialized", e.is_init}, {"tick", e.current_tick},
                {"committed", committed}, {"pending", pending}, {"docks", docks},
                {"arrivals", arrivals}, {"order", e.initializationOrder},
                {"history", e.committedHistory}};
    }
};

struct TestCase {
    std::string name;
    std::string description;
    std::function<void()> body;
};
inline std::vector<TestCase>& tests() { static std::vector<TestCase> cases; return cases; }
inline void test(std::string name, std::string description, std::function<void()> body) {
    tests().push_back({std::move(name), std::move(description), std::move(body)});
}
inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
inline void expect_json(const json& actual, const json& expected) {
    // Comparing dump() also checks field order and array order.
    if (actual.dump() != expected.dump())
        throw std::runtime_error("Expected: " + expected.dump() + "\nActual:   " + actual.dump());
}
inline void must_ok(const json& response) {
    require(response.at("status") == "ok", "Fixture setup failed: " + response.dump());
}
inline const std::string NOT_INIT = "Simulation must be initialized first.";
inline constexpr auto MAX_COUNT = std::numeric_limits<std::int64_t>::max();

inline json unit(int destination, std::int64_t quantity = 1) {
    return {{"destination", destination}, {"quantity", quantity}};
}
inline json dock_unit(int destination, std::int64_t quantity = 1, int location = 0) {
    return {{"location", location}, {"destination", destination}, {"quantity", quantity}};
}
inline json empty_cargo() { return json::array(); }
inline json initial_cargo() { return json::array({dock_unit(2,3), dock_unit(1,2)}); }
inline json after_loading() { return json::array({dock_unit(2,2), dock_unit(1,2)}); }
inline json request(const std::string& id = "v1", int destination = 2) {
    return {{"vessel_id", id}, {"destination", destination}};
}
inline json swap_request(int load = 1, int unload = 2) {
    return {{"vessel_id", "v1"}, {"load_destination", load}, {"unload_destination", unload}};
}
inline json setup(bool with_cargo = false) {
    json p = {{"travel_times", {{0,4,7},{4,0,3},{7,3,0}}},
              {"dock_throughput", {4,4,2}},
              {"vessels", {{{"id","v1"},{"location",0}},{{"id","v2"},{"location",2}}}}};
    if (with_cargo) p["cargo_manifest"] = initial_cargo();
    return p;
}
inline json parked(std::string state = "idle", int location = 0, json cargo = empty_cargo()) {
    return {{"state", state}, {"location", location}, {"cargo", cargo}};
}
inline json traveling(int to, std::int64_t arrival, json cargo = empty_cargo(), int from = 0) {
    return {{"state","transit"},{"from",from},{"to",to},{"arrives_at_tick",arrival},{"cargo",cargo}};
}
inline json world(std::int64_t tick, json v1 = parked(), json cargo = empty_cargo()) {
    return {{"status","ok"},{"payload",{{"tick",tick},
        {"vessels",{{"v1",v1},{"v2",parked("idle",2)}}},{"cargo",cargo}}}};
}
inline SimulationEngine initialized(bool cargo = true) {
    SimulationEngine e; must_ok(e.init(setup(cargo))); return e;
}
inline SimulationEngine docked(bool cargo = true) {
    auto e = initialized(cargo); must_ok(e.dock(request())); must_ok(e.tick()); return e;
}
inline SimulationEngine loaded() {
    auto e = docked(); must_ok(e.load(request())); must_ok(e.tick()); return e;
}
inline SimulationEngine idle_loaded() {
    auto e = loaded(); must_ok(e.undock(request())); must_ok(e.tick()); return e;
}
inline SimulationEngine moving(bool cargo = true) {
    auto e = cargo ? idle_loaded() : initialized(false);
    must_ok(e.set_destination(request())); must_ok(e.tick()); return e;
}
inline void expect_error(SimulationEngine& e, const std::function<json()>& action,
                         const std::string& message) {
    const auto before = SimulationEngineTestAccess::snapshot(e);
    const auto actual = action();
    expect_json(actual, {{"status","error"},{"payload",{{"message",message}}}});
    expect_json(SimulationEngineTestAccess::snapshot(e), before);
}
