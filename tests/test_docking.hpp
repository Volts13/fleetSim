#pragma once
#include "test_support.hpp"

inline void register_docking_tests() {
    test("dock.success.empty_inventory", "Plan docking at tick 0 with empty cargo arrays.", [] {
        auto e=initialized(false);expect_json(e.dock(request()),world(0,parked("docked")));
    });
    test("dock.success.loaded_vessel", "A loaded idle vessel retains cargo when docking.", [] {
        auto e=idle_loaded();expect_json(e.dock(request()),
            world(3,parked("docked",0,json::array({unit(2)})),after_loading()));
    });
    test("dock.success.override_departure", "A pending trip is replaced by docking and never enters the arrival queue.", [] {
        auto e=initialized();must_ok(e.set_destination(request()));
        expect_json(e.dock(request()),world(0,parked("docked"),initial_cargo()));
        for(int i=0;i<7;++i)must_ok(e.tick());
        expect_json(e.get_state(),world(7,parked("docked"),initial_cargo()));
    });
    test("dock.success.repeat_pending_dock", "Repeated pending docks are idempotent while committed state is idle.", [] {
        auto e=initialized();must_ok(e.dock(request()));
        expect_json(e.dock(request()),world(0,parked("docked"),initial_cargo()));
    });
    test("undock.success.empty_inventory", "Plan idle after committed docking, retaining empty cargo arrays.", [] {
        auto e=docked(false);expect_json(e.undock(request()),world(1));
    });
    test("undock.success.loaded_vessel", "Keep committed vessel cargo when undocking.", [] {
        auto e=loaded();expect_json(e.undock(request()),world(2,parked("idle",0,json::array({unit(2)})),after_loading()));
    });
    test("undock.success.pending_load_preserved", "Load then undock in the same tick keeps the newly loaded unit.", [] {
        auto e=docked();must_ok(e.load(request()));
        expect_json(e.undock(request()),world(1,parked("idle",0,json::array({unit(2)})),after_loading()));
    });
    test("undock.success.repeat_pending_undock", "Repeated pending undocks do not lose cargo or spend throughput.", [] {
        auto e=loaded();must_ok(e.undock(request()));const auto before=SimulationEngineTestAccess::snapshot(e);
        expect_json(e.undock(request()),world(2,parked("idle",0,json::array({unit(2)})),after_loading()));
        expect_json(SimulationEngineTestAccess::snapshot(e),before);
    });

    using Method=json (SimulationEngine::*)(const json&);
    for(const auto& item: std::vector<std::pair<std::string,Method>>{{"dock",&SimulationEngine::dock},{"undock",&SimulationEngine::undock}}) {
        const auto name=item.first;const auto method=item.second;
        test(name+".error.before_init","Reject the command before init.",[method] {
            SimulationEngine e;expect_error(e,[&]{return (e.*method)(request());},NOT_INIT);
        });
        test(name+".error.unknown_vessel","Reject an unknown ID in a cargo-bearing world.",[method] {
            auto e=loaded();expect_error(e,[&]{return (e.*method)(request("missing"));},"Invalid vessel ID.");
        });
        for(const auto& fields: std::vector<std::pair<std::string,json>>{
            {"missing_id",json::object()},{"id_wrong_type",{{"vessel_id",7}}},{"parameters_not_object",nullptr}}) {
            test(name+".error."+fields.first,"Reject malformed parameters; preserve all cargo and pending state.",[name,method,fields] {
                auto e=loaded();expect_error(e,[&]{return (e.*method)(fields.second);},"Invalid parameters for "+name+".");
            });
        }
    }
    test("dock.error.committed_docked","Dock requires committed idle, even if already docked.",[] {
        auto e=loaded();expect_error(e,[&]{return e.dock(request());},"vessel v1 is not idle");
    });
    test("dock.error.committed_transit","Cannot dock while traveling.",[] {
        auto e=moving();expect_error(e,[&]{return e.dock(request());},"vessel v1 is not idle");
    });
    test("dock.error.pending_undock","Must commit undock before docking again.",[] {
        auto e=loaded();must_ok(e.undock(request()));expect_error(e,[&]{return e.dock(request());},"vessel v1 is not idle");
    });
    test("undock.error.committed_idle","Cannot undock an idle vessel.",[] {
        auto e=idle_loaded();expect_error(e,[&]{return e.undock(request());},"vessel v1 is not docked");
    });
    test("undock.error.committed_transit","Cannot undock while traveling.",[] {
        auto e=moving();expect_error(e,[&]{return e.undock(request());},"vessel v1 is not docked");
    });
    test("undock.error.pending_dock","Must commit a newly requested dock before undocking.",[] {
        auto e=idle_loaded();must_ok(e.dock(request()));
        expect_error(e,[&]{return e.undock(request());},"vessel v1 is not docked");
    });
}
