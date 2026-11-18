/* One source, three builds: wasm32 (WAMR), riscv64 (libriscv, as godot-sandbox runs it), and the host (native control).
 * Floats cross the host API as their 32-bit patterns so both VMs pass plain integers. */
#include <stdint.h>

#if defined(__wasm__)
#define HOST_IMPORT(n) __attribute__((import_module("host"), import_name(#n)))
HOST_IMPORT(host_get) int32_t host_get(int32_t id, int32_t axis);
HOST_IMPORT(host_set) void host_set(int32_t id, int32_t axis, int32_t bits);
#define EXPORT __attribute__((used, visibility("default")))
#elif defined(__riscv)
#define ECALL_GET 500
#define ECALL_SET 501
static inline long ecall3(long n, long a, long b, long c) {
	register long a0 __asm__("a0") = a;
	register long a1 __asm__("a1") = b;
	register long a2 __asm__("a2") = c;
	register long a7 __asm__("a7") = n;
	__asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
	return a0;
}
static int32_t host_get(int32_t id, int32_t axis) { return (int32_t)ecall3(ECALL_GET, id, axis, 0); }
static void host_set(int32_t id, int32_t axis, int32_t bits) { ecall3(ECALL_SET, id, axis, bits); }
#define EXPORT __attribute__((used, visibility("default")))
void _start(void) { for (;;) { } }
/* The compiler turns the 16-element copy loops into memcpy calls, and a freestanding guest has no libc: optnone keeps this from turning into a call to itself. */
__attribute__((used, optnone)) void *memcpy(void *d, const void *s, unsigned long n) {
	unsigned char *dd = d; const unsigned char *ss = s;
	while (n--) { *dd++ = *ss++; }
	return d;
}
#else
int32_t host_get(int32_t id, int32_t axis);
void host_set(int32_t id, int32_t axis, int32_t bits);
#define EXPORT
#endif

static float bits_f(int32_t b) { float f; __builtin_memcpy(&f, &b, 4); return f; }
static int32_t f_bits(float f) { int32_t b; __builtin_memcpy(&b, &f, 4); return b; }

/* Problem 1 */
EXPORT void empty(void) { }

/* Problem 2: n transforms, each read (3 axes) and written back moved by (+1, +2, +3). Returns the sum of the values read, in the low bits. */
EXPORT int32_t transforms(int32_t n) {
	float sum = 0.0f;
	for (int32_t i = 0; i < n; i++) {
		float x = bits_f(host_get(i, 0)), y = bits_f(host_get(i, 1)), z = bits_f(host_get(i, 2));
		sum += x + y + z;
		host_set(i, 0, f_bits(x + 1.0f));
		host_set(i, 1, f_bits(y + 2.0f));
		host_set(i, 2, f_bits(z + 3.0f));
	}
	return f_bits(sum);
}

/* Problem 3: the first update of a freshly instantiated module. */
static int32_t ticks;
EXPORT int32_t update(void) { return ++ticks; }

/* Problem 4: L-BFGS (m = 5, two-loop recursion, Armijo backtracking) finds the saddle point of
 * h(x, y) = 1/2 x'Ax - 1/2 y'By + x'Cy by minimising |grad h|^2 = |Mz - b|^2, z = (x, y), n = 16.
 * An "iteration" is one L-BFGS step; a solve that converges (|g| < 1e-9) or reaches 2000 steps restarts from the next
 * pseudo-random start, until `iters` steps have run. Returns the bit pattern of the sum of the final residuals. */
#define N 16
#define M 5
static double Mat[N][N], B[N];
static uint32_t rng;
static double frand(void) { rng = rng * 1664525u + 1013904223u; return (double)(rng >> 8) / 16777216.0 - 0.5; }
static void setup(void) {
	rng = 12345u;
	for (int i = 0; i < N; i++) {
		for (int j = 0; j < N; j++) { Mat[i][j] = 0.1 * frand(); }
		Mat[i][i] += (i < N / 2) ? 2.0 + 0.25 * (i % 3) : -(2.0 + 0.25 * (i % 3));
		B[i] = frand();
	}
	for (int i = 0; i < N / 2; i++) { for (int j = 0; j < N / 2; j++) {
		Mat[i][j] = Mat[j][i] = 0.5 * (Mat[i][j] + Mat[j][i]);
		Mat[N / 2 + i][N / 2 + j] = Mat[N / 2 + j][N / 2 + i] = 0.5 * (Mat[N / 2 + i][N / 2 + j] + Mat[N / 2 + j][N / 2 + i]);
	} }
}
static double fg(const double *z, double *g) {
	double r[N], f = 0.0;
	for (int i = 0; i < N; i++) { double s = -B[i]; for (int j = 0; j < N; j++) { s += Mat[i][j] * z[j]; } r[i] = s; f += s * s; }
	for (int j = 0; j < N; j++) { double s = 0.0; for (int i = 0; i < N; i++) { s += Mat[i][j] * r[i]; } g[j] = 2.0 * s; }
	return f;
}
EXPORT int64_t lbfgs_saddle(int32_t iters) {
	static double S[M][N], Y[M][N], rho[M];
	double z[N], g[N], d[N], zn[N], gn[N], alpha[M], total = 0.0;
	setup();
	int32_t done = 0;
	while (done < iters) {
		for (int i = 0; i < N; i++) { z[i] = 4.0 * frand(); }
		double f = fg(z, g);
		int count = 0, head = 0;
		for (int step = 0; step < 2000 && done < iters; step++, done++) {
			double gn2 = 0.0;
			for (int i = 0; i < N; i++) { gn2 += g[i] * g[i]; }
			if (gn2 < 1e-18) { break; }
			for (int i = 0; i < N; i++) { d[i] = -g[i]; }
			for (int k = 0; k < count; k++) {
				int idx = (head - 1 - k + M) % M; double a = 0.0;
				for (int i = 0; i < N; i++) { a += S[idx][i] * d[i]; }
				a *= rho[idx]; alpha[k] = a;
				for (int i = 0; i < N; i++) { d[i] -= a * Y[idx][i]; }
			}
			if (count > 0) {
				int idx = (head - 1 + M) % M; double yy = 0.0, sy = 0.0;
				for (int i = 0; i < N; i++) { yy += Y[idx][i] * Y[idx][i]; sy += S[idx][i] * Y[idx][i]; }
				double gamma = sy / yy;
				for (int i = 0; i < N; i++) { d[i] *= gamma; }
			}
			for (int k = count - 1; k >= 0; k--) {
				int idx = (head - 1 - k + M) % M; double b = 0.0;
				for (int i = 0; i < N; i++) { b += Y[idx][i] * d[i]; }
				b *= rho[idx];
				for (int i = 0; i < N; i++) { d[i] += S[idx][i] * (alpha[k] - b); }
			}
			double gd = 0.0;
			for (int i = 0; i < N; i++) { gd += g[i] * d[i]; }
			if (gd >= 0.0) { for (int i = 0; i < N; i++) { d[i] = -g[i]; } gd = -gn2; count = 0; }
			double t = count == 0 ? 1.0 / (1.0 + __builtin_sqrt(gn2)) : 1.0, fn = f;
			for (int ls = 0; ls < 40; ls++) {
				for (int i = 0; i < N; i++) { zn[i] = z[i] + t * d[i]; }
				fn = fg(zn, gn);
				if (fn <= f + 1e-4 * t * gd) { break; }
				t *= 0.5;
			}
			double sy = 0.0;
			for (int i = 0; i < N; i++) { S[head][i] = zn[i] - z[i]; Y[head][i] = gn[i] - g[i]; sy += S[head][i] * Y[head][i]; }
			if (sy > 1e-300) { rho[head] = 1.0 / sy; head = (head + 1) % M; if (count < M) { count++; } }
			for (int i = 0; i < N; i++) { z[i] = zn[i]; g[i] = gn[i]; }
			f = fn;
		}
		total += f;
	}
	int64_t out; __builtin_memcpy(&out, &total, 8); return out;
}
