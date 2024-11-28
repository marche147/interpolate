## interpolate

Just for fun.

Test with:

```
mkdir build
cd build
cmake ..
make check
```

### Why?

It was an attempt to tackle with the problem of Symbolic Execution Engines generating deeply nested ITE (If-Then-Else) symbolic statements when performing symbolic reads (i.e., the index is symbolic). An observation of such statements is that it usually takes very long for the underlying SMT solver to solve them, so the core idea of this repo is simple: turn them into polynomials (over finite fields) using Lagrange interpolation, then (maybe) it will make the life of SMT solvers easier.
Unfortunately I didn't test this idea, and there are many foreseeable problems alongside:
* It can only deal with constant arrays. (I kinda feel there are ways to update? Not sure though...)
* If the array is highly "non-linear", the resulting polynomial will be of high degree.
* ...

Still I find it intriguing to combine something I learnt into an actual (not really) working implementation. It was fun!
