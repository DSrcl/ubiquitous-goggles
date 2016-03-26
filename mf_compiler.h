#ifndef _MF_COMPILER_H_
#define _MF_COMPILER_H_

bool compileToObjectFile(llvm::Module &M,
                         llvm::MachineFunction &MF,
                         const std::string &OutFilename,
                         llvm::TargetMachine *TM);

#endif
