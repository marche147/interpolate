typedef long long int64_t;

int64_t modpow(int64_t a, int64_t x, int64_t m) {
  int64_t result = 1;
  while (x > 0) {
    if (x & 1)
      result = (result * a) % m;
    a = (a * a) % m;
    x >>= 1;
  }
  return result;
}
