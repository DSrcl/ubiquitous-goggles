#include <llvm/ADT/EquivalenceClasses.h>
#include <iostream>

using namespace llvm;

int main() {
  EquivalenceClasses<int> X;
  X.insert(1);
  X.insert(2);
  X.insert(3);
  X.insert(4);
  X.unionSets(1, 2);
  X.unionSets(3, 4);
  for (auto CI = X.begin(); CI != X.end(); CI++) {
    for (auto I = X.member_begin(CI); I != X.member_end(); I++) {
      std::cout << *I << " ";
    }
    std::cout << "\n----x-------\n";
  }
}
