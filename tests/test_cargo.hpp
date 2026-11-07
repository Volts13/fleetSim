#pragma once
#include "test_support.hpp"

inline void register_cargo_successes() {
    test("load.success.empty_vessel", "Load one unit into an empty vessel; dock quantity decreases by one.", [] {
        auto e=docked();expect_json(e.load(request()),world(1,parked("docked",0,json::array({unit(2)})),after_loading()));
    });
    test("load.success.existing_vessel_cargo", "Loading a matching destination increments its quantity instead of duplicating its entry.", [] {
        auto e=loaded();expect_json(e.load(request()),world(2,parked("docked",0,json::array({unit(2,2)})),
            json::array({dock_unit(2),dock_unit(1,2)})));
    });
    test("load.success.last_dock_unit", "Taking the last unit removes the dock's zero-quantity entry.", [] {
        SimulationEngine e;auto p=setup();p["cargo_manifest"]=json::array({dock_unit(2)});
        must_ok(e.init(p));must_ok(e.dock(request()));must_ok(e.tick());
        expect_json(e.load(request()),world(1,parked("docked",0,json::array({unit(2)}))));
    });
    test("unload.success.last_vessel_unit", "Unload the final unit at a nondestination dock; vessel cargo becomes [].", [] {
        auto e=loaded();expect_json(e.unload(request()),world(2,parked("docked"),initial_cargo()));
    });
    test("unload.success.other_cargo_remains", "Unload one destination while preserving cargo for another.", [] {
        auto e=loaded();must_ok(e.load(request("v1",1)));must_ok(e.tick());
        expect_json(e.unload(request()),world(3,parked("docked",0,json::array({unit(1)})),
            json::array({dock_unit(2,3),dock_unit(1)})));
    });
    test("unload.success.deliver_at_destination", "Cargo unloaded at its own destination disappears instead of entering dock inventory.", [] {
        auto e=idle_loaded();must_ok(e.set_destination(request()));
        for(int i=0;i<7;++i)must_ok(e.tick());
        must_ok(e.dock(request()));must_ok(e.tick());
        expect_json(e.unload(request()),world(11,parked("docked",2),after_loading()));
    });
    test("swap.success.different_destinations", "Exchange one unit in each direction using one operation.", [] {
        auto e=loaded();expect_json(e.swap(swap_request()),world(2,parked("docked",0,json::array({unit(1)})),
            json::array({dock_unit(2,3),dock_unit(1)})));
    });
    test("swap.success.same_destination", "Exchanging identical cargo preserves inventory and still spends one operation.", [] {
        auto e=loaded();expect_json(e.swap(swap_request(2,2)),world(2,parked("docked",0,json::array({unit(2)})),after_loading()));
        for(int i=0;i<3;++i)must_ok(e.swap(swap_request(2,2)));
        expect_error(e,[&]{return e.swap(swap_request());},"transfer of 5 operations exceeds dock throughput of 4 at location 0");
    });
    test("swap.success.delivery", "The unload half delivers at the current location while the load half succeeds.", [] {
        SimulationEngine e;auto p=setup();p["cargo_manifest"]=json::array({dock_unit(0),dock_unit(1)});
        must_ok(e.init(p));must_ok(e.dock(request()));must_ok(e.tick());
        must_ok(e.load(request("v1",0)));must_ok(e.tick());
        expect_json(e.swap(swap_request(1,0)),world(2,parked("docked",0,json::array({unit(1)}))));
    });
    test("swap.success.identical_destination_delivery", "When both destinations equal location, vessel quantity stays constant and dock loses one unit.", [] {
        SimulationEngine e;auto p=setup();p["cargo_manifest"]=json::array({dock_unit(0,2)});
        must_ok(e.init(p));must_ok(e.dock(request()));must_ok(e.tick());
        must_ok(e.load(request("v1",0)));must_ok(e.tick());
        expect_json(e.swap(swap_request(0,0)),world(2,parked("docked",0,json::array({unit(0)}))));
    });
    test("load.success.load_then_unload_cost_two", "A load followed by unload uses two operations even when inventory returns to its initial value.", [] {
        SimulationEngine e;auto p=setup(true);p["dock_throughput"][0]=2;
        must_ok(e.init(p));must_ok(e.dock(request()));must_ok(e.tick());
        must_ok(e.load(request()));expect_json(e.unload(request()),world(1,parked("docked"),initial_cargo()));
        expect_error(e,[&]{return e.load(request());},"transfer of 3 operations exceeds dock throughput of 2 at location 0");
    });
}

inline void register_cargo_errors() {
    using Method=json (SimulationEngine::*)(const json&);
    for (const auto& item : std::vector<std::pair<std::string,Method>>{
        {"load",&SimulationEngine::load},{"unload",&SimulationEngine::unload},{"swap",&SimulationEngine::swap}}) {
        const auto name=item.first;const auto method=item.second;
        const json valid=name=="swap"?swap_request():request();
        test(name+".error.before_init","Reject before init; no cargo-bearing state can exist yet.",[method,valid] {
            SimulationEngine e;expect_error(e,[&]{return (e.*method)(valid);},NOT_INIT);
        });
        test(name+".error.unknown_vessel","Unknown ID; existing vessel and dock cargo remain untouched.",[method,valid] {
            auto e=loaded();auto p=valid;p["vessel_id"]="missing";
            expect_error(e,[&]{return (e.*method)(p);},"Invalid vessel ID.");
        });
        for(const auto& fields:std::vector<std::pair<std::string,json>>{
            {"missing_id",json::object()},{"id_wrong_type",{{"vessel_id",7}}},{"parameters_not_object",nullptr}}) {
            test(name+".error."+fields.first,"Malformed parameters return an error without changing cargo or counters.",[method,fields] {
                auto e=loaded();expect_error(e,[&]{return (e.*method)(fields.second);},"Invalid parameters for cargo operation.");
            });
        }
        for(const std::string state:{"committed_idle","committed_transit","pending_dock","pending_undock"}) {
            test(name+".error."+state,"Require both committed and planned state to be docked.",[method,valid,state] {
                auto e=state=="committed_transit"?moving():state=="pending_undock"?loaded():idle_loaded();
                if(state=="pending_dock")must_ok(e.dock(request()));
                if(state=="pending_undock")must_ok(e.undock(request()));
                expect_error(e,[&]{return (e.*method)(valid);},"vessel v1 is not docked");
            });
        }
        const std::vector<std::string> keys=name=="swap"?
            std::vector<std::string>{"load_destination","unload_destination"}:std::vector<std::string>{"destination"};
        for(const auto& key:keys) {
            test(name+".error.missing_"+key,"Missing cargo destination produces a malformed-parameters error.",[method,valid,key] {
                auto e=loaded();auto p=valid;p.erase(key);
                expect_error(e,[&]{return (e.*method)(p);},"Invalid parameters for cargo operation.");
            });
            for(const auto& value:std::vector<std::pair<std::string,json>>{
                {"negative",-1},{"too_large",3},{"fractional",1.5},{"boolean",true},{"string","1"},{"huge",MAX_COUNT}}) {
                test(name+".error."+key+"_"+value.first,"Invalid cargo destination is rejected before any transfer.",[method,valid,key,value] {
                    auto e=loaded();auto p=valid;p[key]=value.second;
                    expect_error(e,[&]{return (e.*method)(p);},"Cargo destination out of bounds.");
                });
            }
        }
        test(name+".error.throughput_exhausted","After four valid swaps, a fifth operation is rejected without changing state.",[method,valid] {
            auto e=loaded();for(int i=0;i<4;++i)must_ok(e.swap(swap_request(2,2)));
            expect_error(e,[&]{return (e.*method)(valid);},"transfer of 5 operations exceeds dock throughput of 4 at location 0");
        });
        if(name!="unload") {
            test(name+".error.cargo_missing_at_dock","Requested load inventory is absent; no budget is spent.",[method,valid,name] {
                auto e=loaded();auto p=valid;p[name=="swap"?"load_destination":"destination"]=0;
                expect_error(e,[&]{return (e.*method)(p);},"Requested cargo is not available at the dock.");
            });
            test(name+".error.vessel_quantity_overflow","Seed maximum onboard count; reject loading another unit atomically.",[method,name] {
                auto e=loaded();SimulationEngineTestAccess::cargo(e,{{2,MAX_COUNT},{1,1}});
                const auto p=name=="swap"?swap_request(2,1):request();
                expect_error(e,[&]{return (e.*method)(p);},"Cargo quantity is out of range.");
            });
        }
        if(name!="load") {
            test(name+".error.cargo_missing_on_vessel","Requested unload inventory is absent; swaps also leave their load untouched.",[method,valid,name] {
                auto e=loaded();auto p=valid;p[name=="swap"?"unload_destination":"destination"]=0;
                expect_error(e,[&]{return (e.*method)(p);},"Requested cargo is not available on the vessel.");
            });
            test(name+".error.dock_quantity_overflow","Seed maximum dock count; reject unloading another unit atomically.",[method,valid] {
                auto e=loaded();SimulationEngineTestAccess::dock_cargo(e,{{2,MAX_COUNT},{1,2}});
                expect_error(e,[&]{return (e.*method)(valid);},"Cargo quantity is out of range.");
            });
        }
    }
    test("swap.error.unload_cannot_supply_load", "Both sources must exist before a swap; its unload cannot create its load inventory.", [] {
        auto e=loaded();SimulationEngineTestAccess::dock_cargo(e,{{1,2}});
        expect_error(e,[&]{return e.swap(swap_request(2,2));},"Requested cargo is not available at the dock.");
    });
}
