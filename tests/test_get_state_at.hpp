#pragma once
#include "test_support.hpp"

// A successful query must return the expected snapshot and leave every part
// of the engine unchanged, including pending commands and dock throughput.
inline void expect_history(SimulationEngine& engine, std::int64_t tick,
                           const json& expected) {
    const auto before = SimulationEngineTestAccess::snapshot(engine);
    expect_json(engine.get_state_at({{"tick", tick}}), expected);
    expect_json(SimulationEngineTestAccess::snapshot(engine), before);
}

inline void register_get_state_at_tests() {
    test("get_state_at.success.tick_zero_empty",
         "No manifest: tick 0 contains idle vessels and empty cargo arrays.", [] {
        auto engine = initialized(false);
        expect_history(engine, 0, world(0));
    });

    test("get_state_at.success.tick_zero_cargo",
         "An init manifest is included in the tick-0 snapshot.", [] {
        auto engine = initialized(true);
        expect_history(engine, 0, world(0, parked(), initial_cargo()));
    });

    test("get_state_at.success.empty_cargo_past_and_current",
         "Without cargo, past transit and current arrival snapshots remain distinct.", [] {
        auto engine = initialized(false);
        must_ok(engine.set_destination(request("v1", 1)));
        for (int tick = 1; tick <= 4; ++tick) must_ok(engine.tick());
        expect_history(engine, 0, world(0));
        expect_history(engine, 3, world(3, traveling(1, 4)));
        expect_history(engine, 4, world(4, parked("idle", 1)));
    });

    test("get_state_at.success.pending_departure_excluded",
         "Overriding a planned trip leaves tick 0 idle; tick 1 records only the final trip.", [] {
        auto engine = initialized(false);
        must_ok(engine.set_destination(request("v1", 2)));
        must_ok(engine.set_destination(request("v1", 1)));
        expect_history(engine, 0, world(0));
        expect_json(engine.get_state(), world(0, traveling(1, 4)));
        must_ok(engine.tick());
        expect_history(engine, 1, world(1, traveling(1, 4)));
    });

    test("get_state_at.success.pending_dock_excluded",
         "A pending dock appears in get_state but not in the current committed snapshot.", [] {
        auto engine = initialized(true);
        must_ok(engine.dock(request()));
        expect_history(engine, 0, world(0, parked(), initial_cargo()));
        expect_json(engine.get_state(), world(0, parked("docked"), initial_cargo()));
        must_ok(engine.tick());
        expect_history(engine, 1, world(1, parked("docked"), initial_cargo()));
    });

    test("get_state_at.success.pending_load_excluded",
         "Loading changes planned inventories; only the next tick records the transfer.", [] {
        auto engine = docked();
        must_ok(engine.load(request()));
        expect_history(engine, 1, world(1, parked("docked"), initial_cargo()));
        must_ok(engine.tick());
        expect_history(engine, 2, world(2,
            parked("docked", 0, json::array({unit(2)})), after_loading()));
        expect_history(engine, 1, world(1, parked("docked"), initial_cargo()));
    });

    test("get_state_at.success.current_boundary_with_cargo",
         "At a tick boundary, the current snapshot equals get_state with vessel and dock cargo.", [] {
        auto engine = loaded();
        const auto expected = world(2,
            parked("docked", 0, json::array({unit(2)})), after_loading());
        expect_json(engine.get_state(), expected);
        expect_history(engine, 2, expected);
    });

    test("get_state_at.success.tick_manifest",
         "New manifest cargo belongs to its successful tick, not the preceding snapshot.", [] {
        auto engine = initialized(false);
        must_ok(engine.tick({{"cargo_manifest", initial_cargo()}}));
        expect_history(engine, 0, world(0));
        expect_history(engine, 1, world(1, parked(), initial_cargo()));
    });

    test("get_state_at.success.independent_return_value",
         "Editing returned JSON cannot change stored history or the live world.", [] {
        auto engine = initialized(true);
        auto returned = engine.get_state_at({{"tick", 0}});
        returned["payload"]["cargo"][0]["quantity"] = 999;
        returned["payload"]["vessels"].erase("v1");
        expect_history(engine, 0, world(0, parked(), initial_cargo()));
        expect_json(engine.get_state(), world(0, parked(), initial_cargo()));
    });

    test("get_state_at.success.arrival_and_delivery",
         "Past snapshots preserve transit and cargo after the vessel arrives and delivers it.", [] {
        auto engine = idle_loaded(); // Tick 3; one destination-2 unit onboard.
        const auto onboard = json::array({unit(2)});
        must_ok(engine.set_destination(request())); // Arrival tick = 3 + 7 = 10.
        for (int tick = 4; tick <= 10; ++tick) must_ok(engine.tick());
        expect_history(engine, 9, world(9, traveling(2, 10, onboard), after_loading()));
        expect_history(engine, 10, world(10, parked("idle", 2, onboard), after_loading()));

        must_ok(engine.dock(request()));
        must_ok(engine.tick()); // Tick 11 commits the dock.
        must_ok(engine.unload(request()));
        expect_history(engine, 11, world(11, parked("docked", 2, onboard), after_loading()));
        must_ok(engine.tick()); // Tick 12 commits the delivery.
        expect_history(engine, 12, world(12, parked("docked", 2), after_loading()));
        expect_history(engine, 3, world(3, parked("idle", 0, onboard), after_loading()));
    });

    test("get_state_at.success.failed_tick_no_snapshot",
         "A rejected manifest creates no snapshot and preserves pending work for a valid tick.", [] {
        auto engine = docked();
        must_ok(engine.load(request()));
        expect_error(engine, [&] {
            return engine.tick({{"cargo_manifest",
                json::array({dock_unit(2), dock_unit(3)})}});
        }, "Cargo location or destination is out of bounds.");
        expect_error(engine, [&] { return engine.get_state_at({{"tick", 2}}); },
                     "tick 2 is in the future (current tick: 1)");
        expect_history(engine, 0, world(0, parked(), initial_cargo()));
        expect_history(engine, 1, world(1, parked("docked"), initial_cargo()));
        must_ok(engine.tick());
        expect_history(engine, 2, world(2,
            parked("docked", 0, json::array({unit(2)})), after_loading()));
    });

    test("get_state_at.success.init_retry",
         "A failed init records nothing; a valid retry records the correct tick 0.", [] {
        SimulationEngine engine;
        auto parameters = setup(true);
        parameters["vessels"][1]["id"] = "v1";
        expect_error(engine, [&] { return engine.init(parameters); },
                     "Invalid parameters for init.");
        expect_error(engine, [&] { return engine.get_state_at({{"tick", 0}}); }, NOT_INIT);
        must_ok(engine.init(setup(true)));
        expect_history(engine, 0, world(0, parked(), initial_cargo()));
    });

    test("get_state_at.success.second_init_preserves_history",
         "Rejected reinitialization cannot clear or replace existing snapshots.", [] {
        auto engine = loaded();
        expect_error(engine, [&] { return engine.init(setup()); },
                     "Simulation has already been initialized.");
        expect_history(engine, 0, world(0, parked(), initial_cargo()));
        expect_history(engine, 2, world(2,
            parked("docked", 0, json::array({unit(2)})), after_loading()));
    });

    test("get_state_at.success.query_preserves_dock_budget",
         "Reading old snapshots does not restore an exhausted dock's throughput.", [] {
        auto engine = loaded();
        for (int operation = 0; operation < 4; ++operation)
            must_ok(engine.swap(swap_request(2, 2)));
        expect_history(engine, 0, world(0, parked(), initial_cargo()));
        expect_history(engine, 2, world(2,
            parked("docked", 0, json::array({unit(2)})), after_loading()));
        expect_error(engine, [&] { return engine.load(request()); },
                     "transfer of 5 operations exceeds dock throughput of 4 at location 0");
    });

    test("get_state_at.success.many_ticks",
         "All 81 snapshots stay correctly indexed after repeated tick advances.", [] {
        auto engine = initialized(true);
        for (int tick = 1; tick <= 80; ++tick) must_ok(engine.tick());
        for (int tick = 0; tick <= 80; ++tick)
            expect_history(engine, tick, world(tick, parked(), initial_cargo()));
    });

    test("get_state_at.error.before_init",
         "Queries before initialization return an error without changing the engine.", [] {
        SimulationEngine engine;
        expect_error(engine, [&] { return engine.get_state_at({{"tick", 0}}); }, NOT_INIT);
    });

    struct InvalidQuery {
        const char* name;
        const char* description;
        json parameters;
        std::string message;
    };
    const std::vector<InvalidQuery> invalid_queries = {
        {"missing_tick", "Reject missing tick.", json::object(),
         "Invalid parameters for get_state_at."},
        {"parameters_array", "Reject an array instead of a parameters object.", json::array(),
         "Invalid parameters for get_state_at."},
        {"parameters_null", "Reject null parameters.", nullptr,
         "Invalid parameters for get_state_at."},
        {"negative", "Reject tick -1.", {{"tick", -1}}, "tick must be non-negative"},
        {"negative_limit", "Reject INT64_MIN without integer wraparound.",
         {{"tick", std::numeric_limits<std::int64_t>::min()}}, "tick must be non-negative"},
        {"float", "Reject floating-point tick 1.0.", {{"tick", 1.0}}, "tick must be an integer"},
        {"boolean", "Reject a boolean tick.", {{"tick", true}}, "tick must be an integer"},
        {"string", "Reject a numeric string tick.", {{"tick", "1"}}, "tick must be an integer"},
        {"null_tick", "Reject null tick.", {{"tick", nullptr}}, "tick must be an integer"},
        {"future", "Reject a tick greater than the current tick.", {{"tick", 99}},
         "tick 99 is in the future (current tick: 2)"},
        {"huge_unsigned", "Reject UINT64_MAX without integer wraparound.",
         {{"tick", std::numeric_limits<std::uint64_t>::max()}},
         "tick 18446744073709551615 is in the future (current tick: 2)"}
    };
    for (const auto& invalid : invalid_queries) {
        test(std::string("get_state_at.error.") + invalid.name,
             std::string(invalid.description) + " Preserve cargo, pending work, and history.",
             [invalid] {
            auto engine = loaded(); // Tick 2; both vessel and dock have cargo.
            must_ok(engine.load(request())); // Leave a pending transfer to protect as well.
            expect_error(engine, [&] { return engine.get_state_at(invalid.parameters); },
                         invalid.message);
            expect_history(engine, 0, world(0, parked(), initial_cargo()));
            expect_history(engine, 1, world(1, parked("docked"), initial_cargo()));
            expect_history(engine, 2, world(2,
                parked("docked", 0, json::array({unit(2)})), after_loading()));
        });
    }
}
