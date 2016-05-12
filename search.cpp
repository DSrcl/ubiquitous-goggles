#include <llvm/Support/raw_ostream.h>

#include <csignal>
#include <cmath>
#include <cstdlib>
#include "search.h"

using namespace llvm;

Searcher::Searcher(TargetMachine *TM,
                   Module *MM,
                   MachineFunction *MF,
                   FunctionType *FnTy,
                   ReplayClient *Cli) :
  M(MM),
  Client(Cli),
  Transform(std::unique_ptr<Transformation>(getTransformation(TM, MF))),
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

    if (resp.signal != 0) {
      cost += Signal_penalty;
    } else {
      cost += (resp.reg_dist + resp.stack_dist + resp.heap_dist);
    }
  }

  return cost;
}

double Searcher::rand()
{
  return (double)std::rand() / (RAND_MAX);
}

void Searcher::transformRewrite()
{
  unsigned MaxInstrs = 15;
  double r = rand();

  // propose a rewrite
  if (Transform->getNumInstrs() == 0) { 
    Transform->Insert();
  } else if (r <= pc) { 
    // opcode
    Transform->MutateOpcode();
  } else if (r <= pc + po) {
    // operand
    Transform->MutateOperand();
  } else if (r <= pc + po + ps) { 
    // swap

    // probability of selecting an instruction
    double InstrProb = (double)Transform->getNumInstrs() / MaxInstrs;
    double SwapProb = InstrProb * InstrProb;
    if (rand() <= SwapProb) {
      Transform->Swap();
    } else {
      Transform->Move();
    }
  } else { 
    // instruction

    double NumInstrs = Transform->getNumInstrs();
    double DelProb = NumInstrs / (double)MaxInstrs * pu,
           RepProb = NumInstrs / (double)MaxInstrs * (1-pu);
    double r = rand();
    if (r < DelProb) {
      Transform->Delete();
    } else if (r < DelProb + RepProb) { 
      Transform->Replace();
    } else {
      Transform->Insert();
    }
  }
}

// default search strategy
MachineFunction *Searcher::synthesize()
{ 
  unsigned cost = 10000, Itr = 0;

  do {
    transformRewrite();

    auto Result = Client->testRewrite(M, TargetTy, Transform->getFunction());
    unsigned newCost = calculateCost(Result);

    bool Accept; 
    if (newCost >= Signal_penalty) {
      Accept = false;
    } else if (newCost <= cost) { 
      Accept = true;
    } else { 
      Accept = (rand() < std::exp(-beta * double(newCost)/double(cost)));
    }

    if (!Accept) {
      Transform->Undo();
    } else {
      Transform->Accept();
      cost = newCost;
    }

    for (auto &I : *Transform->getFunction()->begin()) {
      errs() << I;
    }

    Itr++;

    errs() << "!!! cost: " << cost << ", new cost: " << newCost << ", " << "itr: " << Itr << "\n";

  } while (cost != 0);


  return Transform->getFunction();
}

MachineFunction *Searcher::copyFunction(MachineFunction *MF)
{
  MachineFunction *Copied = new MachineFunction(MF->getFunction(),
                                                MF->getTarget(),
                                                1, MF->getMMI());
  for (auto &MBB : *MF) {
    auto *MBB_ = Copied->CreateMachineBasicBlock();
    Copied->push_back(MBB_);
    for (auto &MI : MBB) {
      MBB_->push_back(Copied->CloneMachineInstr(&MI));
    }
  }

  return Copied;
}

unsigned Searcher::calculateLatency(MachineFunction *MF)
{
  assert(MF->size() == 1 && "branches not supported");
  return MF->begin()->size() * 5;
}

MachineFunction *Searcher::optimize(int MaxItrs)
{
  unsigned cost = 100000, bestCorrectCost;
  MachineFunction *bestCorrect = copyFunction(Transform->getFunction());
  bestCorrectCost = calculateLatency(bestCorrect);

  for (int i = 0; i < MaxItrs; i++) {
    transformRewrite();

    double r = rand();
    // max cost with which we accept a rewrite
    unsigned maxCost = cost - (std::log(r) / beta);

    // reject without testing
    if (maxCost < calculateLatency(Transform->getFunction())) {
      Transform->Undo();
      continue;
    }

    auto Result = Client->testRewrite(M, TargetTy, Transform->getFunction());

    unsigned dist = calculateCost(Result),
             newCost = dist + calculateLatency(Transform->getFunction());

    bool Accept;

    if (dist >= Signal_penalty) {
      Accept = false;
    } else if (newCost <= cost) {
      Accept = true;
    } else {
      Accept = newCost < maxCost;
    }

    if (Accept) {
      Transform->Accept();
      cost = newCost;
    } else {
      Transform->Undo();
    }

    errs() << "!!! Optimizing\n";

    if (dist == 0 && newCost < bestCorrectCost) {
      bestCorrectCost = newCost;
      if (bestCorrect) delete bestCorrect;
      bestCorrect = copyFunction(Transform->getFunction());
    }

    for (auto &I : *Transform->getFunction()->begin()) {
      errs() << I;
    }
    
    errs() << "----- cost: " << newCost << ", dist: " << dist
      << ", instrs: " << Transform->getNumInstrs() << "\n";
  }

  return bestCorrect;
}
