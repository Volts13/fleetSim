#pragma once
#include "test_init.hpp"

inline void register_tick_tests() {
    test("tick.success.empty_inventory", "Advance from tick 0 to 1 with empty cargo arrays.", [] {
        auto e=initialized(false); expect_json(e.tick(),world(1));
    });
    test("tick.success.add_manifest", "Add cargo at tick 1 without requiring an init manifest.", [] {
        auto e=initialized(false);
        expect_json(e.tick({{"cargo_manifest",initial_cargo()}}),world(1,parked(),initial_cargo()));
    });
    test("tick.success.commit_loaded_cargo", "Commit a pending load and preserve cargo in the response.", [] {
        auto e=docked(); must_ok(e.load(request()));
        expect_json(e.tick(),world(2,parked("docked",0,json::array({unit(2)})),after_loading()));
    });
    test("tick.success.empty_manifest_preserves_inventory", "An explicit empty manifest adds nothing and deletes nothing.", [] {
        auto e=loaded(); expect_json(e.tick({{"cargo_manifest",empty_cargo()}}),
            world(3,parked("docked",0,json::array({unit(2)})),after_loading()));
    });
    test("tick.success.exact_arrival_with_cargo", "Remain transit until tick 10, then become idle with cargo intact.", [] {
        auto e=idle_loaded(); must_ok(e.set_destination(request()));
        for (int tick=4; tick<10; ++tick)
            expect_json(e.tick(),world(tick,traveling(2,10,json::array({unit(2)})),after_loading()));
        expect_json(e.tick(),world(10,parked("idle",2,json::array({unit(2)})),after_loading()));
    });
    test("tick.success.simultaneous_arrivals", "Process every vessel due at tick 1.", [] {
        SimulationEngine e; auto p=setup(true); p["travel_times"]={{0,1,1},{1,0,1},{1,1,0}};
        must_ok(e.init(p)); must_ok(e.set_destination(request("v1",1))); must_ok(e.set_destination(request("v2",1)));
        expect_json(e.tick(),{{"status","ok"},{"payload",{{"tick",1},
            {"vessels",{{"v1",parked("idle",1)},{"v2",parked("idle",1)}}},{"cargo",initial_cargo()}}}});
    });
    test("tick.success.reset_shared_budget", "A successful tick restores operations spent by either vessel at the dock.", [] {
        SimulationEngine e; auto p=setup(true); p["dock_throughput"][0]=1; p["vessels"][1]["location"]=0;
        must_ok(e.init(p)); must_ok(e.dock(request())); must_ok(e.dock(request("v2"))); must_ok(e.tick());
        must_ok(e.load(request()));
        expect_error(e,[&]{return e.load(request("v2"));},"transfer of 2 operations exceeds dock throughput of 1 at location 0");
        must_ok(e.tick()); must_ok(e.load(request("v2")));
    });
    test("tick.error.before_init", "Reject tick before initialization.", [] {
        SimulationEngine e; expect_error(e,[&]{return e.tick();},NOT_INIT);
    });
    test("tick.error.clock_limit", "At INT64_MAX reject the tick and preserve cargo and pending changes.", [] {
        auto e=loaded(); SimulationEngineTestAccess::clock(e,MAX_COUNT);
        expect_error(e,[&]{return e.tick();},"Simulation tick limit reached.");
    });
    test("tick.error.parameters_not_object", "Reject scalar parameters before advancing time.", [] {
        auto e=loaded(); expect_error(e,[&]{return e.tick(7);},"Parameters must be an object.");
    });
    for (const auto& c : invalid_manifests()) {
        test("tick.error."+c.name,"Cargo-bearing fixture: preserve clock, pending load, inventories, queue, and budget.",[c] {
            auto e=docked(); must_ok(e.load(request()));
            expect_error(e,[&]{return e.tick({{"cargo_manifest",c.value}});},
                         c.message=="$generic"?"Invalid parameters for tick.":c.message);
            expect_json(e.tick(),world(2,parked("docked",0,json::array({unit(2)})),after_loading()));
        });
    }
}

inline void register_destination_tests() {
    test("set_destination.success.empty_inventory", "Plan 0-to-1 arrival at tick 4 with empty cargo.", [] {
        auto e=initialized(false); expect_json(e.set_destination(request("v1",1)),world(0,traveling(1,4)));
    });
    test("set_destination.success.loaded_vessel", "At tick 3, plan 0-to-2 arrival at 10 and retain onboard cargo.", [] {
        auto e=idle_loaded(); expect_json(e.set_destination(request()),
            world(3,traveling(2,10,json::array({unit(2)})),after_loading()));
    });
    test("set_destination.success.override_pending_trip", "Replace arrival 7 with arrival 4 before tick; discarded trip never arrives.", [] {
        auto e=initialized(); must_ok(e.set_destination(request()));
        expect_json(e.set_destination(request("v1",1)),world(0,traveling(1,4),initial_cargo()));
        for(int i=0;i<7;++i) must_ok(e.tick());
        expect_json(e.get_state(),world(7,parked("idle",1),initial_cargo()));
    });
    test("set_destination.success.override_pending_dock", "An idle committed vessel may replace a pending dock with travel.", [] {
        auto e=initialized(); must_ok(e.dock(request()));
        expect_json(e.set_destination(request()),world(0,traveling(2,7),initial_cargo()));
    });
    test("set_destination.success.current_location", "Replace a pending departure with a zero-duration trip, idle next tick.", [] {
        auto e=idle_loaded(); must_ok(e.set_destination(request()));
        expect_json(e.set_destination(request("v1",0)),world(3,traveling(0,3,json::array({unit(2)})),after_loading()));
        expect_json(e.tick(),world(4,parked("idle",0,json::array({unit(2)})),after_loading()));
    });
    test("set_destination.error.before_init", "Reject a destination before initialization.", [] {
        SimulationEngine e; expect_error(e,[&]{return e.set_destination(request());},NOT_INIT);
    });
    test("set_destination.error.unknown_vessel", "Reject an unknown ID; existing cargo remains unchanged.", [] {
        auto e=idle_loaded(); expect_error(e,[&]{return e.set_destination(request("missing"));},"Invalid vessel ID.");
    });
    for(const auto& item: std::vector<std::pair<std::string,json>>{
        {"missing_id",{{"destination",1}}},{"id_wrong_type",{{"vessel_id",7},{"destination",1}}},
        {"missing_destination",{{"vessel_id","v1"}}},{"parameters_not_object",nullptr}}) {
        test("set_destination.error."+item.first,"Malformed fields return the destination validation error.",[item] {
            auto e=idle_loaded(); expect_error(e,[&]{return e.set_destination(item.second);},"Invalid parameters for set_destination.");
        });
    }
    for(const auto& item: std::vector<std::pair<std::string,json>>{
        {"destination_negative",-1},{"destination_too_large",3},{"destination_fractional",1.5},
        {"destination_boolean",true},{"destination_string","1"},{"destination_huge",MAX_COUNT}}) {
        test("set_destination.error."+item.first,"Reject an invalid destination without losing the existing plan or cargo.",[item] {
            auto e=idle_loaded(); must_ok(e.set_destination(request())); auto p=request();p["destination"]=item.second;
            expect_error(e,[&]{return e.set_destination(p);},"Destination out of bounds.");
        });
    }
    test("set_destination.error.committed_transit", "A committed trip cannot be overridden.", [] {
        auto e=moving(); expect_error(e,[&]{return e.set_destination(request("v1",1));},"vessel v1 is not idle.");
    });
    test("set_destination.error.committed_docked", "Docked vessels must undock and commit before departure.", [] {
        auto e=loaded(); expect_error(e,[&]{return e.set_destination(request());},"vessel v1 is not idle.");
    });
    test("set_destination.error.pending_undock", "Planned idle is insufficient while committed state is docked.", [] {
        auto e=loaded();must_ok(e.undock(request()));
        expect_error(e,[&]{return e.set_destination(request());},"vessel v1 is not idle.");
    });
    test("set_destination.error.arrival_overflow", "Reject current tick plus travel time exceeding INT64_MAX.", [] {
        auto e=idle_loaded();SimulationEngineTestAccess::clock(e,MAX_COUNT-1);
        expect_error(e,[&]{return e.set_destination(request());},"Arrival tick is out of range.");
    });
}

inline void register_get_state_tests() {
    test("get_state.success.empty_inventory", "Return the exact empty-cargo world without changing state.", [] {
        auto e=initialized(false);const auto before=SimulationEngineTestAccess::snapshot(e);
        expect_json(e.get_state(),world(0));expect_json(SimulationEngineTestAccess::snapshot(e),before);
    });
    test("get_state.success.with_inventory", "Include both vessel cargo and dock inventory.", [] {
        auto e=loaded();expect_json(e.get_state(),world(2,parked("docked",0,json::array({unit(2)})),after_loading()));
    });
    test("get_state.success.pending_cargo_visible", "Show a pending load immediately while committed cargo is still empty.", [] {
        auto e=docked();must_ok(e.load(request()));
        expect_json(e.get_state(),world(1,parked("docked",0,json::array({unit(2)})),after_loading()));
    });
    test("get_state.success.initialization_order", "Preserve nonalphabetical initialization order and field ordering.", [] {
        SimulationEngine e; auto p=setup(true);p["vessels"]={{{"id","z"},{"location",0}},{{"id","a"},{"location",1}}};
        must_ok(e.init(p));expect_json(e.get_state(),{{"status","ok"},{"payload",{{"tick",0},
            {"vessels",{{"z",parked()},{"a",parked("idle",1)}}},{"cargo",initial_cargo()}}}});
    });
    test("get_state.error.before_init", "The only public get_state rejection is a call before initialization.", [] {
        SimulationEngine e;expect_error(e,[&]{return e.get_state();},NOT_INIT);
    });
}
