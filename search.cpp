#include <llvm/Support/raw_ostream.h>

#include <csignal>
#include <cmath>
#include <cstdlib>
#include "search.h"

////////////
#include "mf_compiler.h"
#include "llvm/IR/Function.h"
////////////

using namespace llvm;

Searcher::Searcher(TargetMachine *TM,
                   Module *MM,
                   MachineFunction *MF,
                   FunctionType *FnTy,
                   ReplayClient *Cli) :
  M(MM),
  Client(Cli),
  Transform(TM, MF),
  TargetTy(FnTy)
{}

unsigned Searcher::calculateCost(std::vector<response> &responses)
{
  unsigned cost = 0;
  for (const auto &resp : responses) {
    if (!resp.success) {
      errs() << "Failed to run rewrite: " << resp.msg << "\n";
      exit(1);
    }

    if (resp.signal == SIGILL) {
      cost = Sigill_penalty;
      break;
    } else if (resp.signal == SIGBUS) {
      cost += Sigbus_penalty;
    } else if (resp.signal == SIGSEGV) {
      cost += Sigsegv_penalty; 
    } else if (resp.signal == SIGFPE) {
      cost += Sigfpe_penalty;
    } else if (resp.signal == 0) {
      cost += (resp.reg_dist + resp.stack_dist + resp.heap_dist);
    } else {
      errs() << "******* signal = " << resp.signal << "\n";
      cost = Unknown_penalty;
    }
  }
  return cost;
}

double Searcher::rand()
{
  return (double)std::rand() / (RAND_MAX);
}

// default search strategy
MachineFunction *Searcher::synthesize()
{ 
  unsigned cost = 100;
  const unsigned MaxInstrs = 5;
  do {
    double r = rand();

    errs() << "------- " << Transform.getNumInstrs() <<"\n";
    // propose a rewrite
    if (Transform.getNumInstrs() == 0) { 
      Transform.Insert();
    } else if (r <= pc) { 
      // opcode
      Transform.MutateOpcode();
    } else if (r <= pc + po) {
      // operand
      Transform.MutateOperand();
    } else if (r <= pc + po + ps) { 
      // swap

      // probability of selecting an instruction
      double InstrProb = (double)Transform.getNumInstrs() / MaxInstrs;
      double SwapProb = InstrProb * InstrProb;
      if (rand() <= SwapProb) {
        Transform.Swap();
      } else {
        Transform.Move();
      }
    } else { 
      // instruction

      double NumInstrs = Transform.getNumInstrs();
      double DelProb = NumInstrs / MaxInstrs * pu,
             RepProb = NumInstrs / MaxInstrs * (1-pu);
      double r = rand();
      if (r < DelProb) {
        Transform.Delete();
      } else if (r < DelProb + RepProb) { 
        Transform.Replace();
      } else {
        Transform.Insert();
      }
    }

    /*
    for (auto &I : *Transform.getFunction()->begin()) {
      errs() << I;
    }
    */

    auto Result = Client->testRewrite(M, TargetTy, Transform.getFunction());
    unsigned newCost = calculateCost(Result);
    bool Accept;
    if (newCost == Sigill_penalty) {
      // never accept a rewrite with illegal intruction
      Accept = false;
    } else if (newCost <= cost) { 
      Accept = true;
    } else { 
      Accept = (rand() < std::exp(-beta * double(newCost)/double(cost)));
    }

    if (!Accept) {
      Transform.Undo();
    } else {
      cost = newCost;
    }

    for (auto &I : *Transform.getFunction()->begin()) {
      errs() << I;
    }



    errs() << "!!! cost: " << cost << ", new cost: " << newCost <<"\n";

  } while (cost != 0);


  return Transform.getFunction();
}
