#include "test_init.hpp"
#include "test_movement.hpp"
#include "test_docking.hpp"
#include "test_cargo.hpp"
#include "test_get_state_at.hpp"

int main(int argc, char** argv) {
    register_init_tests(); register_tick_tests(); register_destination_tests();
    register_get_state_tests(); register_docking_tests();
    register_cargo_successes(); register_cargo_errors();
    register_get_state_at_tests();
    std::string filter = argc > 1 ? argv[1] : "";
    if(filter == "--list") {
        for(const auto& c:tests())std::cout<<c.name<<'\t'<<c.description<<'\n';
        return 0;
    }
    if(filter == "--catalog-json") {
        json list=json::array();
        for(const auto& c:tests())list.push_back({{"name",c.name},{"description",c.description}});
        std::cout<<list.dump(2)<<'\n';return 0;
    }
    int passed=0,failed=0;
    for(const auto& c:tests()) {
        if(!filter.empty() && c.name.compare(0,filter.size(),filter)!=0)continue;
        try {c.body();++passed;std::cout<<"PASS "<<c.name<<'\n';}
        catch(const std::exception& e) {++failed;std::cout<<"FAIL "<<c.name<<"\n  "<<c.description<<"\n  "<<e.what()<<'\n';}
    }
    if(passed+failed==0) {std::cerr<<"No tests match: "<<filter<<'\n';return 2;}
    std::cout<<passed<<" passed, "<<failed<<" failed\n";
    return failed?1:0;
}
