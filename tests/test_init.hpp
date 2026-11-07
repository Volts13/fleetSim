#pragma once
#include "test_support.hpp"

struct ManifestCase { std::string name; json value; std::string message; };
inline std::vector<ManifestCase> invalid_manifests() {
    const std::string bounds = "Cargo location or destination is out of bounds.";
    const std::string positive = "Cargo quantity must be a positive integer.";
    return {
        {"manifest_null", nullptr, "cargo_manifest must be an array."},
        {"manifest_object", json::object(), "cargo_manifest must be an array."},
        {"manifest_entry_not_object", json::array({7}), "$generic"},
        {"manifest_missing_location", json::array({{{"destination",1},{"quantity",1}}}), "$generic"},
        {"manifest_missing_destination", json::array({{{"location",0},{"quantity",1}}}), "$generic"},
        {"manifest_missing_quantity", json::array({{{"location",0},{"destination",1}}}), "$generic"},
        {"manifest_location_negative", json::array({dock_unit(1,1,-1)}), bounds},
        {"manifest_location_too_large", json::array({dock_unit(1,1,3)}), bounds},
        {"manifest_destination_negative", json::array({dock_unit(-1)}), bounds},
        {"manifest_destination_too_large", json::array({dock_unit(3)}), bounds},
        {"manifest_location_wrong_type", json::array({{{"location",true},{"destination",1},{"quantity",1}}}), bounds},
        {"manifest_destination_wrong_type", json::array({{{"location",0},{"destination","1"},{"quantity",1}}}), bounds},
        {"quantity_zero", json::array({dock_unit(1,0)}), positive},
        {"quantity_negative", json::array({dock_unit(1,-1)}), positive},
        {"quantity_fractional", json::array({{{"location",0},{"destination",1},{"quantity",1.5}}}), positive},
        {"quantity_boolean", json::array({{{"location",0},{"destination",1},{"quantity",true}}}), positive},
        {"quantity_string", json::array({{{"location",0},{"destination",1},{"quantity","1"}}}), positive},
        {"quantity_above_int64", json::array({{{"location",0},{"destination",1},
             {"quantity",static_cast<std::uint64_t>(MAX_COUNT)+1}}}), "Cargo quantity is out of range."},
        {"quantity_sum_overflow", json::array({dock_unit(1,MAX_COUNT),dock_unit(1)}), "Cargo quantity is out of range."},
        {"valid_then_invalid_entry", json::array({dock_unit(2),dock_unit(3)}), bounds}
    };
}

inline void register_init_tests() {
    test("init.success.empty_inventory", "Omit manifest; tick 0 and empty vessel/dock cargo arrays.", [] {
        SimulationEngine e; expect_json(e.init(setup()), world(0));
    });
    test("init.success.with_manifest", "Manifest populates docks; vessels begin idle with empty cargo.", [] {
        SimulationEngine e; expect_json(e.init(setup(true)), world(0,parked(),initial_cargo()));
    });
    test("init.success.empty_manifest", "Explicit [] has the same result as an omitted manifest.", [] {
        SimulationEngine e; auto p=setup(); p["cargo_manifest"]=empty_cargo();
        expect_json(e.init(p),world(0));
    });
    test("init.success.merge_duplicates", "Repeated destinations combine quantities without changing first-insertion order.", [] {
        SimulationEngine e; auto p=setup(true); p["cargo_manifest"].push_back(dock_unit(2,2));
        expect_json(e.init(p),world(0,parked(),json::array({dock_unit(2,5),dock_unit(1,2)})));
    });
    test("init.success.empty_fleet", "An empty vessel list prints an object, not null or an array.", [] {
        SimulationEngine e; auto p=setup(true); p["vessels"]=json::array();
        expect_json(e.init(p),{{"status","ok"},{"payload",{{"tick",0},{"vessels",json::object()},{"cargo",initial_cargo()}}}});
    });
    test("init.error.already_initialized", "Second init is rejected without replacing existing cargo or vessel state.", [] {
        auto e=loaded(); expect_error(e,[&]{return e.init(setup());},"Simulation has already been initialized.");
    });

    auto bad = [](std::string name, std::function<void(json&)> mutate) {
        test("init.error."+name, "Invalid init with a cargo manifest; reject atomically, then permit a valid retry.", [mutate] {
            SimulationEngine e; auto p=setup(true); mutate(p);
            expect_error(e,[&]{return e.init(p);},"Invalid parameters for init.");
            expect_json(e.init(setup(true)),world(0,parked(),initial_cargo()));
        });
    };
    bad("parameters_not_object",[](json& p){p=nullptr;});
    bad("missing_matrix",[](json& p){p.erase("travel_times");});
    bad("matrix_not_array",[](json& p){p["travel_times"]=7;});
    bad("matrix_empty",[](json& p){p["travel_times"]=json::array();});
    bad("row_not_array",[](json& p){p["travel_times"][1]=7;});
    bad("matrix_not_square",[](json& p){p["travel_times"][1]=json::array({1});});
    bad("diagonal_nonzero",[](json& p){p["travel_times"][0][0]=1;});
    bad("off_diagonal_zero",[](json& p){p["travel_times"][0][1]=0;});
    bad("travel_time_negative",[](json& p){p["travel_times"][0][1]=-1;});
    bad("travel_time_fractional",[](json& p){p["travel_times"][0][1]=1.5;});
    bad("travel_time_boolean",[](json& p){p["travel_times"][0][1]=true;});
    bad("travel_time_above_int",[](json& p){p["travel_times"][0][1]=MAX_COUNT;});
    bad("missing_vessels",[](json& p){p.erase("vessels");});
    bad("vessels_not_array",[](json& p){p["vessels"]=json::object();});
    bad("vessel_entry_not_object",[](json& p){p["vessels"][0]=7;});
    bad("vessel_id_missing",[](json& p){p["vessels"][0].erase("id");});
    bad("vessel_id_not_string",[](json& p){p["vessels"][0]["id"]=7;});
    bad("duplicate_vessel_id",[](json& p){p["vessels"][1]["id"]="v1";});
    bad("location_missing",[](json& p){p["vessels"][0].erase("location");});
    bad("location_negative",[](json& p){p["vessels"][0]["location"]=-1;});
    bad("location_too_large",[](json& p){p["vessels"][0]["location"]=3;});
    bad("location_wrong_type",[](json& p){p["vessels"][0]["location"]=false;});
    bad("throughput_missing",[](json& p){p.erase("dock_throughput");});
    bad("throughput_not_array",[](json& p){p["dock_throughput"]=nullptr;});
    bad("throughput_wrong_length",[](json& p){p["dock_throughput"]={1,2};});
    bad("throughput_zero",[](json& p){p["dock_throughput"][0]=0;});
    bad("throughput_negative",[](json& p){p["dock_throughput"][0]=-1;});
    bad("throughput_fractional",[](json& p){p["dock_throughput"][0]=1.5;});
    bad("throughput_boolean",[](json& p){p["dock_throughput"][0]=true;});
    bad("throughput_above_int",[](json& p){p["dock_throughput"][0]=MAX_COUNT;});

    for (const auto& c : invalid_manifests()) {
        test("init.error."+c.name,"Reject malformed or overflowing cargo; initialization remains retryable.",[c] {
            SimulationEngine e; auto p=setup(true); p["cargo_manifest"]=c.value;
            expect_error(e,[&]{return e.init(p);},c.message=="$generic"?"Invalid parameters for init.":c.message);
            expect_json(e.init(setup(true)),world(0,parked(),initial_cargo()));
        });
    }
}
