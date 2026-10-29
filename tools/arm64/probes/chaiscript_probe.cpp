#include <cstdio>
#include "chaiscript/chaiscript.hpp"
int main() {
    chaiscript::ChaiScript chai;
    if (chai.eval<int>("var values = [1, 2, 3]; values[0] + values[1] + values[2];") != 6) return 1;
    std::puts("PASS: bundled ChaiScript evaluates a script with vector indexing");
    return 0;
}
