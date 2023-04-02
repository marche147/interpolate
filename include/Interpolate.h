#ifndef _INTERPOLATE_H
#define _INTERPOLATE_H

#include <algorithm>
#include <cstdint>
#include <random>
#include <tuple>
#include <vector>

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <llvm/Support/raw_ostream.h>

using Point = std::pair<int64_t, int64_t>;
using Poly = std::vector<int64_t>;

bool IsValid(const llvm::GlobalVariable &GV);
std::vector<Point> ExtractIndexValuePairs(const llvm::GlobalVariable &GV);
bool IsPrime(int64_t Number, uint64_t K);
void PolyPrint(const Poly &P);
std::tuple<Poly, int64_t> LagrangeInterpolate(const std::vector<Point> &Points);

#endif