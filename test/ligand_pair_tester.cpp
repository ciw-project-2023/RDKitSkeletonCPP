//
// Created by follenburg on 15.11.23.
//

#include "catch2/catch.hpp"
//#include "../src/multialign/Forward.hpp"
#include "../src/multialign/BasicClasses/LigandPair.hpp"

TEST_CASE("test_ligand_pair", "[ligand_pair_tester]") {
    MultiAlign::LigandPair pair1(0 ,1);
    MultiAlign::LigandPair pair2(1 ,0);
    MultiAlign::LigandPair pair3(2 ,1);
    MultiAlign::LigandPair pair4(0 ,1);

    CHECK(pair1.getFirst() == 0);
    CHECK(pair1.getSecond() == 1);
    CHECK(pair2.getFirst() == 0);
    CHECK(pair2.getSecond() == 1);
    CHECK(pair1 == pair2);
    CHECK(!(pair1 == pair3));
    CHECK(pair1 == pair4);
    //CHECK();// check for assertion

}