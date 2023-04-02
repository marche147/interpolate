#include <llvm/Pass.h>

#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalObject.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>

#if LLVM_VERSION_MAJOR >= 13
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

#if LLVM_VERSION_MAJOR >= 14
#include <llvm/Passes/OptimizationLevel.h>
#else
using OptimizationLevel = llvm::PassBuilder::OptimizationLevel;
#endif
#endif
#include <string>

#include "Compile.h"
#include "Interpolate.h"

using namespace llvm;

static FunctionCallee getModPowFunction(Module &M) {
  auto *I64Type = IntegerType::get(M.getContext(), 64);
  auto *FuncType =
      FunctionType::get(I64Type, {I64Type, I64Type, I64Type}, false);
  return M.getOrInsertFunction("modpow", FuncType);
}

Function *buildPolynomialFunction(Module &M, StringRef VariableName,
                                  const Poly &P, int64_t Modulus) {
  auto *I32Type = IntegerType::get(M.getContext(), 32);
  auto *I64Type = IntegerType::get(M.getContext(), 64);
  auto *F = Function::Create(FunctionType::get(I32Type, {I64Type}, false),
                             GlobalValue::LinkageTypes::PrivateLinkage,
                             "poly_" + VariableName, M);
  auto *BB = BasicBlock::Create(M.getContext(), "entry", F);
  IRBuilder<> IRB(BB);

  auto Callee = getModPowFunction(M);
  auto *Arg = F->getArg(0);

  // Calculate monomial terms.
  std::vector<Value *> Monomials;
  for (size_t i = 0; i < P.size(); i++) {
    Value *V = nullptr;
    if (i == 0) {
      V = ConstantInt::get(I64Type, P[0]);
    } else {
      V = IRB.CreateCall(Callee, {Arg, ConstantInt::get(I64Type, i),
                                  ConstantInt::get(I64Type, Modulus)});
      V = IRB.CreateMul(V, ConstantInt::get(I64Type, P[i]));
    }
    Monomials.push_back(V);
  }

  // Add and mod.
  auto *Result = Monomials[0];
  for (size_t i = 1; i < P.size(); i++) {
    Result = IRB.CreateAdd(Result, Monomials[i]);
  }
  Result = IRB.CreateURem(Result, ConstantInt::get(I64Type, Modulus));

  // Extract and return.
  auto *RetVal = IRB.CreateTrunc(Result, I32Type);
  IRB.CreateRet(RetVal);
  return F;
}

static void rewriteLoadInst(GlobalVariable &GV, Function *Polynomial) {
  FunctionCallee Callee(Polynomial->getFunctionType(), Polynomial);
  std::vector<std::pair<LoadInst *, Value *>> LoadInsts;
  std::vector<GetElementPtrInst *> GEPInsts;

  for (auto *U : GV.users()) {
    if (auto *GEPInst = dyn_cast<GetElementPtrInst>(U)) {
      auto *Index = GEPInst->getOperand(2);
      for (auto *User : GEPInst->users()) {
        auto *LI = dyn_cast<LoadInst>(User);
        LoadInsts.push_back({LI, Index});
      }
      GEPInsts.push_back(GEPInst);
    } else if (auto *GEPOp = dyn_cast<GEPOperator>(U)) {
      auto *Index = GEPOp->getOperand(2);
      auto *LI = dyn_cast<LoadInst>(GEPOp->user_back());
      LoadInsts.push_back({LI, Index});
    } else if (auto *CastOp = dyn_cast<BitCastOperator>(U)) {
      continue; // Annotation
    } else {
      llvm_unreachable("unhandled use");
    }
  }

  // Replace each LoadInst with CallInst
  for (auto [LI, Index] : LoadInsts) {
    auto *CI = CallInst::Create(Callee, {Index});
    ReplaceInstWithInst(LI, CI);
  }

  // Remove redundant GEPInst
  for (auto *GEPInst : GEPInsts) {
    GEPInst->eraseFromParent();
  }
  return;
}

static bool handleRewrite(Module &M, GlobalVariable &GV) {
  // Collect all possible rewrites, bailout if there's no rule
  // to rewrite.
  for (auto *U : GV.users()) {
    if (auto *GEPInst = dyn_cast<GetElementPtrInst>(U)) {
      // Check if it has two indices and if the first index is 0
      if (GEPInst->getNumOperands() != 3) {
        return false;
      }
      if (auto *IDX0 = dyn_cast<ConstantInt>(GEPInst->getOperand(1))) {
        if (IDX0->getValue() != 0) {
          return false;
        }
      }

      // Find all users and see if there's only LoadInst.
      for (auto *User : GEPInst->users()) {
        if (auto *LI = dyn_cast<LoadInst>(User)) {
          continue;
        } else if (auto *SI = dyn_cast<StoreInst>(User)) {
          return false;
        } else {
          return false;
        }
      }
    } else if (auto *GEPOp = dyn_cast<GEPOperator>(U)) {
      assert(GEPOp->hasOneUser() && "GEP constants should have only one user.");

      // Check the operands. (cstptr, idx0 = 0, idx1)
      if (GEPOp->getNumOperands() != 3) {
        return false;
      }
      if (auto *IDX0 = dyn_cast<ConstantInt>(GEPOp->getOperand(1))) {
        if (IDX0->getValue() != 0) {
          return false;
        }
      } else {
        return false;
      }

      // Check if the user is LoadInst.
      auto *User = GEPOp->user_back();
      if (auto *LI = dyn_cast<LoadInst>(User)) {
        continue;
      } else if (auto *SI = dyn_cast<StoreInst>(User)) {
        // We can't handle StoreInst
        return false;
      } else {
        // We can't handle unknown inst.
        return false;
      }
    } else if (auto *CastOp = dyn_cast<BitCastOperator>(U)) {
      // Check if U is used in annotations.
      assert(CastOp->hasOneUser() &&
             "BitCastOperator should have only one user.");
      auto *User = CastOp->user_back();
      if (auto *AnnoStruct = dyn_cast<ConstantStruct>(User)) {
        if (AnnoStruct->hasOneUser()) {
          if (auto *AnnoArray =
                  dyn_cast<ConstantArray>(AnnoStruct->user_back())) {
            if (AnnoArray->hasOneUser()) {
              auto *GVar = dyn_cast<GlobalVariable>(AnnoArray->user_back());
              if (!GVar || GVar->getName() != "llvm.global.annotations") {
                return false;
              }
            } else {
              return false;
            }
          } else {
            return false;
          }
        } else {
          return false;
        }
      } else {
        return false;
      }
    } else {
      return false;
    }
  }

  // Now we know we can handle everything.
  // Extract the polynomial first.
  auto Points = ExtractIndexValuePairs(GV);
  auto [P, Modulus] = LagrangeInterpolate(Points);

  // Build the function of polynomial.
  auto *PolyF = buildPolynomialFunction(M, GV.getName(), P, Modulus);

  // Rewrite the LoadInsts.
  rewriteLoadInst(GV, PolyF);

  return true;
}

static bool interpolateTable(Module &M, GlobalVariable &GV) {
  if (!IsValid(GV)) {
    errs() << __FUNCTION__ << ": Skipping " << GV.getName()
           << ", reason: Wrong type for interpolation.\n";
    return false;
  }
  if (!handleRewrite(M, GV)) {
    errs() << __FUNCTION__ << ": Skipping " << GV.getName()
           << ", reason: Not rewritable.\n";
    return false;
  }
  return true;
}

bool transformModule(Module &M) {
  bool Changed = false;
  SmallVector<Constant *, 8> entry;
  SmallVector<GlobalVariable *, 8> GVs;

  auto *Annotation = M.getNamedGlobal("llvm.global.annotations");
  if (Annotation) {
    auto *Arr = cast<ConstantArray>(Annotation->getOperand(0));
    for (size_t i = 0; i < Arr->getNumOperands(); i++) {
      auto *AnnoStruct = cast<ConstantStruct>(Arr->getOperand(i));
      auto *Value = AnnoStruct->getOperand(0)->getOperand(
          0); // Embedded in bitcast constant
      if (auto *GV = dyn_cast<GlobalVariable>(Value)) {
        auto Anno =
            cast<ConstantDataArray>(
                cast<GlobalVariable>(AnnoStruct->getOperand(1)->getOperand(0))
                    ->getOperand(0))
                ->getAsCString();
        if (Anno == "interpolate") {
          if (interpolateTable(M, *GV)) {
            Changed = true;
            GVs.push_back(GV);
          } else {
            entry.push_back(AnnoStruct);
          }
        } else {
          entry.push_back(AnnoStruct);
        }
      } else {
        entry.push_back(AnnoStruct);
      }
    }

    // Reconstruct Annotation array, removing rewritten entries.
    auto *NewAnnotation = ConstantArray::get(
        ArrayType::get(Arr->getType()->getElementType(), entry.size()), entry);
    if (entry.size() > 0) {
      Annotation->setInitializer(NewAnnotation);
    } else {
      Annotation->eraseFromParent();
    }

    // Remove interpolated GVs
    for (auto *GV : GVs) {
      GV->eraseFromParent();
    }
  }

  assert(!verifyModule(M) && "Invalid module after transformation.");
  return Changed;
}

#pragma region legacypm
struct InterpolateLegacyPass : public ModulePass {
  static char ID;
  InterpolateLegacyPass() : ModulePass(ID) {}
  bool runOnModule(Module &M) override { return transformModule(M); }
};
char InterpolateLegacyPass::ID = 0;

void addInterpolateLegacyPass(const PassManagerBuilder &,
                              legacy::PassManagerBase &PM) {
  PM.add(new InterpolateLegacyPass());
}

static RegisterPass<InterpolateLegacyPass> X("interpolate",
                                             "Interpolation pass");
static struct RegisterStandardPasses Y(PassManagerBuilder::EP_EarlyAsPossible,
                                       addInterpolateLegacyPass);
static struct RegisterStandardPasses
    Z(PassManagerBuilder::EP_EnabledOnOptLevel0, addInterpolateLegacyPass);
#pragma endregion

#pragma region newpm
#if LLVM_VERSION_MAJOR >= 13
class InterpolatePass : public llvm::PassInfoMixin<InterpolatePass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &) {
    return transformModule(M) ? PreservedAnalyses::none()
                              : PreservedAnalyses::all();
  }
  static bool isRequired() { return true; }
};

PassPluginLibraryInfo getInterpolatePluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Interpolation pass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &PM, OptimizationLevel) {
                  PM.addPass(InterpolatePass());
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getInterpolatePluginInfo();
}
#endif
#pragma endregion