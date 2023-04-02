#include "Interpolate.h"

using namespace llvm;

std::random_device RD;
std::mt19937_64 RNG(RD());

bool IsValid(const GlobalVariable &GV) {
  auto *Type = GV.getValueType();

  if (!Type->isArrayTy()) {
    return false;
  }

  // For now we just do uint32_t/int32_t [].
  auto ElemType = dyn_cast<ArrayType>(Type)->getArrayElementType();
  if (ElemType->isIntegerTy()) {
    auto IntType = dyn_cast<IntegerType>(ElemType);
    if (IntType->getIntegerBitWidth() != 32) {
      return false;
    }
  }

  // Ensure it is constant and has initializer.
  if (!GV.hasInitializer() || !GV.isConstant()) {
    return false;
  }
  return true;
}

std::vector<Point> ExtractIndexValuePairs(const GlobalVariable &GV) {
  std::vector<Point> Result;

  assert(IsValid(GV) && "GV of the given type is not supported.");

  auto *Type = dyn_cast<ArrayType>(GV.getValueType());
  auto Size = Type->getArrayNumElements();
  auto *Init = dyn_cast<ConstantDataArray>(GV.getInitializer());

  assert(Init->getNumElements() == Size &&
         "Initializer should always has the same number of elements as the "
         "type does.");

  for (unsigned Index = 0; Index < Init->getNumElements(); Index++) {
    auto *ConstantValue =
        dyn_cast<ConstantInt>(Init->getElementAsConstant(Index));
    int64_t Value = static_cast<int64_t>(ConstantValue->getZExtValue());
    Result.push_back(std::make_pair(Index, Value));
  }
  return Result;
}

template <typename T> static T modpow(T base, T exp, T modulus) {
  base %= modulus;
  T result = 1;
  while (exp > 0) {
    if (exp & 1)
      result = (result * base) % modulus;
    base = (base * base) % modulus;
    exp >>= 1;
  }
  return result;
}

template <typename T> static T gcd(T a, T b) {
  T t;
  while (b != 0) {
    if (a < b) {
      t = a;
      a = b;
      b = t;
      continue;
    }
    t = b;
    b = a % b;
    a = t;
  }
  return a;
}

template <typename T> static std::tuple<T, T, T> egcd(T a, T b) {
  if (a == 0) {
    return {b, 0, 1};
  }
  auto [gcd, x, y] = egcd(b % a, a);
  return {gcd, y - (b / a) * x, x};
}

template <typename T> static inline T mod(T a, T m) {
  auto r = a % m;
  return r < 0 ? r + m : r;
}

template <typename T> static T inverse(T a, T m) {
  auto [g, x, y] = egcd(a, m);
  assert(g == 1 && "Multiplicative inverse does not exist.");
  return mod(x, m);
}

static bool MillerRabin(int64_t D, int64_t N) {
  std::uniform_int_distribution<int64_t> Dist(2, N - 2);

  int64_t A = Dist(RNG);
  int64_t X = modpow(A, D, N);

  if (X == 1 || X == N - 1)
    return true;

  while (D != N - 1) {
    X = (X * X) % N;
    D <<= 1;

    if (X == 1)
      return false;
    if (X == N - 1)
      return true;
  }
  return false;
}

bool IsPrime(int64_t Number, uint64_t K) {
  if (Number == 0 || Number == 1 || Number == 4)
    return false;
  if (Number == 2 || Number == 3)
    return true;
  if ((Number & 1) == 0)
    return false;

  int64_t D = Number - 1;
  while ((D & 1) == 0)
    D >>= 1;

  while (K-- > 0) {
    if (!MillerRabin(D, Number))
      return false;
  }
  return true;
}

#pragma region Interpolation
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static int64_t GetModulus(const std::vector<Point> &Points) {
  auto max_iter = std::max_element(
      Points.begin(), Points.end(),
      [](const Point &a, const Point &b) { return a.second < b.second; });

  assert(max_iter != Points.end() && "Iterator went pass the vector.");
  auto modulus = max_iter->second + 100;

  // Find next prime
  while (true) {
    if (IsPrime(modulus, 20))
      break;
    modulus += 1;
  }
  return modulus;
}

static Poly PolyRemoveLeadingZeroTerm(const Poly &A) {
  size_t newdeg = A.size() - 1;
  while (A[newdeg] == 0 && newdeg != 0) {
    newdeg--;
  }
  Poly P(newdeg + 1, 0);
  for (size_t i = 0; i <= newdeg; i++) {
    P[i] = A[i];
  }
  return P;
}

static Poly PolyAdd(const Poly &A, const Poly &B, uint64_t Modulus) {
  size_t deg = A.size() > B.size() ? A.size() : B.size();
  Poly P(deg, 0);

  for (size_t i = 0; i < deg; i++) {
    uint64_t a = 0, b = 0;
    if (i < A.size())
      a = A[i];
    if (i < B.size())
      b = B[i];
    P[i] = (a + b) % Modulus;
  }
  return PolyRemoveLeadingZeroTerm(P);
}

static Poly PolyMult(const Poly &A, const Poly &B, uint64_t Modulus) {
  size_t newdeg = A.size() + B.size() - 2;
  Poly P;

  P.resize(newdeg + 1);
  for (size_t degA = 0; degA < A.size(); degA++) {
    for (size_t degB = 0; degB < B.size(); degB++) {
      P[degA + degB] = (P[degA + degB] + A[degA] * B[degB]) % Modulus;
    }
  }
  return PolyRemoveLeadingZeroTerm(P);
}

static Poly LagrangeBasis(const std::vector<Point> &Points, int64_t J,
                          int64_t Modulus) {
  int64_t Divisor = 1;
  Poly P = {1};

  for (size_t i = 0; i < Points.size(); i++) {
    auto &Pt = Points[i];
    if (Pt.first == J)
      continue;
    P = PolyMult(P, {mod(Modulus - Pt.first, Modulus), 1}, Modulus);
    Divisor = (Divisor * mod(J - Pt.first, Modulus)) % Modulus;
  }

  auto DivInv = inverse<int64_t>(Divisor, Modulus);
  return PolyMult(P, {DivInv}, Modulus);
}

void PolyPrint(const Poly &P) {
  if (P.size() == 1 && P[0] == 0) {
    errs() << "0\n";
    return;
  }

  for (size_t i = 0; i < P.size(); i++) {
    if (P[i] != 0) {
      if (i == 0) {
        errs() << P[0];
      } else {
        errs() << P[i] << "*x^" << i;
      }
      if (i != P.size() - 1) {
        errs() << " + ";
      }
    }
  }
  errs() << '\n';
}

std::tuple<Poly, int64_t>
LagrangeInterpolate(const std::vector<Point> &Points) {
  auto Modulus = GetModulus(Points);
  Poly Polynomial;

  for (size_t i = 0; i < Points.size(); i++) {
    Poly Basis = LagrangeBasis(Points, i, Modulus);
    Polynomial = PolyAdd(Polynomial,
                         PolyMult(Basis, {Points[i].second}, Modulus), Modulus);
  }
  return {Polynomial, Modulus};
}
#pragma GCC diagnostic pop
#pragma endregion