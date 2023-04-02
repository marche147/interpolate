#ifndef _COMPILE_H
#define _COMPILE_H

#include <llvm/IR/Module.h>
#include <llvm/Support/RandomNumberGenerator.h>
#include <memory>
#include <string>

// There's some problem when trying to use COMPILE_C in an out-of-tree
// LLVM pass, need to look into it.
// #define COMPILE_C

using ModContext = std::pair<std::unique_ptr<llvm::Module>,
                             std::unique_ptr<llvm::LLVMContext>>;

std::unique_ptr<llvm::Module> CompileModuleIR(llvm::StringRef IRCode,
                                              llvm::LLVMContext &Context);
#ifdef COMPILE_C
ModContext CompileToIR(const std::string &FilePath,
                       llvm::ArrayRef<const char *> ExtraArgs = {});
ModContext CompileToIRFromCode(const std::string &Code,
                               llvm::ArrayRef<const char *> ExtraArgs = {});
std::unique_ptr<llvm::Module>
CompileToIRFromCodeWithContext(const std::string &Code,
                               llvm::LLVMContext &Context,
                               llvm::ArrayRef<const char *> ExtraArgs = {});
#endif

#endif