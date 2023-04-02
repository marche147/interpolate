#include "Compile.h"

#ifdef COMPILE_C
#include <clang/CodeGen/BackendUtil.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Tooling/Tooling.h>

using namespace clang;
using namespace clang::tooling;
#endif

#include <llvm/ADT/SmallVector.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#if defined __has_include && __has_include(<filesystem>)
#include <filesystem>
#else
#error "Program requires filesystem library."
#endif
#include <fstream>
#include <string>

#include <unistd.h>

static std::string RandomString(size_t Length) {
  static const char kAlphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  uint8_t *Array = new uint8_t[Length];

  memset(Array, 0, Length);
  llvm::getRandomBytes(Array, Length);

  std::string Result = "";
  for (size_t i = 0; i < Length; i++) {
    Result += kAlphabet[Array[i] % (sizeof(kAlphabet) - 1)];
  }
  return Result;
}

// Can not use textual IR on LLVMContext with that discardValueNames()
// https://reviews.llvm.org/D17946?id=50175
std::unique_ptr<llvm::Module> CompileModuleIR(llvm::StringRef IRCode,
                                              llvm::LLVMContext &Context) {
  llvm::SMDiagnostic Error;
  auto Module = llvm::parseIR(llvm::MemoryBufferRef(IRCode, RandomString(8)),
                              Error, Context);
  if (!Module) {
    Error.print("IRCode", llvm::errs());
    return nullptr;
  }
  return Module;
}

#ifdef COMPILE_C
// https://groups.google.com/g/llvm-dev/c/S5SfPC7pgWc
static std::unique_ptr<llvm::Module> SwitchContext(const llvm::Module &M,
                                                   llvm::LLVMContext &Ctx) {
  llvm::SmallVector<char, 256> Buffer;
  llvm::BitcodeWriter Writer(Buffer);
  Writer.writeModule(M);

  llvm::StringRef Buf(Buffer.data(), Buffer.size());
  auto MemBuf = llvm::MemoryBuffer::getMemBuffer(Buf);
  auto EM = llvm::parseBitcodeFile(*MemBuf, Ctx);

  if (!EM) {
    return nullptr;
  }
  return std::move(EM.get());
}

ModContext CompileToIR(const std::string &FilePath,
                       llvm::ArrayRef<const char *> ExtraArgs) {
  // Setup diagnostic engine.
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts(new DiagnosticOptions());
  TextDiagnosticPrinter *DiagClient =
      new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  DiagnosticsEngine Diags(DiagID, DiagOpts, DiagClient);

  // Setup compiler invocation.
  CompilerInstance Compiler;
  auto CI = std::make_unique<CompilerInvocation>();
  llvm::SmallVector<const char *, 8> Args = {FilePath.c_str()};
  if (ExtraArgs.size() != 0) {
    Args.insert(Args.end(), ExtraArgs.begin(), ExtraArgs.end());
  }

  if (!CompilerInvocation::CreateFromArgs(*CI, Args, Diags)) {
    return std::make_pair(nullptr, nullptr);
  }

  Compiler.setInvocation(std::move(CI));
  Compiler.createDiagnostics();
  if (!Compiler.hasDiagnostics()) {
    return std::make_pair(nullptr, nullptr);
  }

  std::unique_ptr<CodeGenAction> Action(new EmitLLVMOnlyAction());
  if (!Compiler.ExecuteAction(*Action)) {
    return std::make_pair(nullptr, nullptr);
  }

  std::unique_ptr<llvm::LLVMContext> Context(Action->takeLLVMContext());
  auto Module = Action->takeModule();
  return std::make_pair(std::move(Module), std::move(Context));
}

ModContext CompileToIRFromCode(const std::string &Code,
                               llvm::ArrayRef<const char *> ExtraArgs) {
  // Simple hack to get things working.
  // Might be a way to pass directly the source code into
  // the compiler interface.
  std::string FileName = "/tmp/";
  FileName += RandomString(8);
  FileName += ".c";

  std::ofstream ofs(FileName, std::ios::out);
  ofs << Code;
  ofs.close();

  auto Result = CompileToIR(FileName, ExtraArgs);

  unlink(FileName.c_str());
  return Result;
}

std::unique_ptr<llvm::Module>
CompileToIRFromCodeWithContext(const std::string &Code,
                               llvm::LLVMContext &Context,
                               llvm::ArrayRef<const char *> ExtraArgs) {
  auto [M, Ctx] = CompileToIRFromCode(Code, ExtraArgs);
  if (!M) {
    return nullptr;
  }
  return SwitchContext(*M, Context);
}
#endif