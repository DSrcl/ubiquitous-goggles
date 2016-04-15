#ifndef _MF_COMPILER_H_
#define _MF_COMPILER_H_

bool compileToObjectFile(llvm::Module &M,
                         llvm::MachineFunction &MF,
                         const std::string &OutFilename,
                         llvm::TargetMachine *TM,
                         bool PrintAsm=false);

bool emitDumpRegistersModule(llvm::TargetMachine *TM, const std::vector<unsigned> &Regs, const std::string &OutFilename);

#endif
