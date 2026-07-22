//
// Created by Alife Zithu on 7/20/26.
//
#include "serdex.h"
#include <fstream>
#include <iostream>

int main() {
    std::ifstream test_file("test.sex");
    serdex::parser parser = {};
    auto value = parser.parse(test_file);
    if (value.has_value()) {
        std::cout << "parsed!" << std::endl;
        std::cout << value.value() << std::endl;
    } else {
        std::cerr << "parse error" << std::endl;
    }
}