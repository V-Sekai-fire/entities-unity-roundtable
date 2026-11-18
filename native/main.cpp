// Command-line roundtable: the four problems on WAMR, libriscv and the native control.
// usage: rtbench_cli <bench.wasm> <bench.elf> [calls=1000000] [transforms=10000] [iters=1000000] [usd.elf]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "rt.h"

extern "C" {
void empty(void);
int32_t transforms(int32_t n);
int32_t update(void);
int64_t lbfgs_saddle(int32_t iters);
int32_t host_get(int32_t id, int32_t axis);
void host_set(int32_t id, int32_t axis, int32_t bits);
}

static std::vector<float> g_store;
static int32_t bits(float f) { int32_t b; memcpy(&b, &f, 4); return b; }
static int32_t store_get(int32_t id, int32_t axis) { return bits(g_store[size_t(id) * 3 + axis]); }
static void store_set(int32_t id, int32_t axis, int32_t b) { memcpy(&g_store[size_t(id) * 3 + axis], &b, 4); }
extern "C" int32_t host_get(int32_t id, int32_t axis) { return store_get(id, axis); }
extern "C" void host_set(int32_t id, int32_t axis, int32_t b) { store_set(id, axis, b); }

static void reset_store(int n) {
	g_store.assign(size_t(n) * 3, 0.0f);
	for (size_t i = 0; i < g_store.size(); i++) { g_store[i] = float(i % 97) * 0.25f; }
}
static uint64_t store_hash() {
	uint64_t h = 1469598103934665603ull;
	for (float f : g_store) { uint32_t b; memcpy(&b, &f, 4); h = (h ^ b) * 1099511628211ull; }
	return h;
}
static std::vector<uint8_t> slurp(const char *p) {
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void row(const char *problem, const char *vm, double s, const char *note) { printf("%-34s %-8s %12.6e s  %s\n", problem, vm, s, note); }

int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: %s bench.wasm bench.elf [calls] [transforms] [iters] [usd.elf]\n", argv[0]); return 2; }
	const int calls = argc > 3 ? atoi(argv[3]) : 1000000, nt = argc > 4 ? atoi(argv[4]) : 10000, iters = argc > 5 ? atoi(argv[5]) : 1000000;
	rt_set_host(store_get, store_set);
	auto wasm = slurp(argv[1]), elf = slurp(argv[2]);
	int fails = 0;

	// Problem 3 first: load, instantiate, first update. Includes reading nothing from disk; bytes are in memory.
	int32_t hw = 0, hr = 0;
	{
		double t = now(); hw = rt_load(RT_WAMR, wasm.data(), wasm.size()); int ok; int64_t r = hw ? rt_call(hw, "update", 0, &ok) : -1; t = now() - t;
		if (!hw || r != 1) { printf("FAIL wamr load/update: %s\n", rt_error()); return 1; }
		row("3 load+instantiate+first update", "wamr", t, "bench.wasm");
		t = now(); hr = rt_load(RT_RISCV, elf.data(), elf.size()); r = hr ? rt_call(hr, "update", 0, &ok) : -1; t = now() - t;
		if (!hr || r != 1) { printf("FAIL riscv load/update: %s\n", rt_error()); return 1; }
		row("3 load+instantiate+first update", "libriscv", t, "bench.elf");
		if (argc > 6) {
			auto usd = slurp(argv[6]);
			t = now(); int32_t hu = rt_load(RT_RISCV, usd.data(), usd.size()); t = now() - t;
			if (hu) { row("3 load+instantiate (usd.elf)", "libriscv", t, "load only: first update needs the sandbox API"); } else { printf("usd.elf: %s\n", rt_error()); }
		}
	}

	int ok = 0;
	// Problem 1
	{
		double t = now(); for (int i = 0; i < calls; i++) { empty(); } t = now() - t;
		row("1 empty export x calls", "native", t, "control");
		int64_t ns = rt_call_n(hw, "empty", 0, calls, &ok); if (!ok) { printf("FAIL %s\n", rt_error()); fails++; }
		row("1 empty export x calls", "wamr", ns * 1e-9, "");
		ns = rt_call_n(hr, "empty", 0, calls, &ok); if (!ok) { printf("FAIL %s\n", rt_error()); fails++; }
		row("1 empty export x calls", "libriscv", ns * 1e-9, "");
	}
	// Problem 2
	{
		reset_store(nt); double t = now(); int32_t sn = transforms(nt); t = now() - t; uint64_t hn = store_hash();
		row("2 transform read+write", "native", t, "control");
		reset_store(nt); int64_t sw; { double t0 = now(); sw = rt_call(hw, "transforms", nt, &ok); row("2 transform read+write", "wamr", now() - t0, ""); }
		uint64_t hww = store_hash();
		reset_store(nt); int64_t sr; { double t0 = now(); sr = rt_call(hr, "transforms", nt, &ok); row("2 transform read+write", "libriscv", now() - t0, ""); }
		uint64_t hrr = store_hash();
		if (int32_t(sw) != sn || int32_t(sr) != sn || hww != hn || hrr != hn) { printf("FAIL transforms disagree: sums %d %d %d, stores %llx %llx %llx\n", sn, int32_t(sw), int32_t(sr), (unsigned long long)hn, (unsigned long long)hww, (unsigned long long)hrr); fails++; }
	}
	// Problem 4
	{
		double t = now(); int64_t bn = lbfgs_saddle(iters); t = now() - t;
		row("4 L-BFGS saddle x iters", "native", t, "control");
		double t0 = now(); int64_t bw = rt_call(hw, "lbfgs_saddle", iters, &ok); row("4 L-BFGS saddle x iters", "wamr", now() - t0, "");
		t0 = now(); int64_t br = rt_call(hr, "lbfgs_saddle", iters, &ok); row("4 L-BFGS saddle x iters", "libriscv", now() - t0, "");
		double d; memcpy(&d, &bn, 8); printf("   residual sum native %.17g, bits equal: wamr %s libriscv %s\n", d, bw == bn ? "yes" : "NO", br == bn ? "yes" : "NO");
		if (bw != bn || br != bn) { fails++; }
	}
	printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
	return fails ? 1 : 0;
}
