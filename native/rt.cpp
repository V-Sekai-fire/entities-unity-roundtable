#include "rt.h"
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <libriscv/machine.hpp>
#include "wasm_export.h"

namespace {
using Machine = riscv::Machine<riscv::RISCV64>;
rt_get_fn g_get;
rt_set_fn g_set;
std::string g_error;
bool g_wamr_ready;

struct Instance {
	int32_t vm = 0;
	std::vector<uint8_t> bytes; /* WAMR keeps pointing into the wasm buffer */
	wasm_module_t module = nullptr;
	wasm_module_inst_t inst = nullptr;
	wasm_exec_env_t env = nullptr;
	std::unique_ptr<Machine> machine;
	std::map<std::string, uint64_t> addrs;
};
// Leaked on purpose: destroying instances at exit runs after WAMR has torn down its locks.
auto &g_instances = *new std::map<int32_t, std::unique_ptr<Instance>>;
int32_t g_next = 1;

int32_t wamr_host_get(wasm_exec_env_t, int32_t id, int32_t axis) { return g_get(id, axis); }
void wamr_host_set(wasm_exec_env_t, int32_t id, int32_t axis, int32_t bits) { g_set(id, axis, bits); }
NativeSymbol g_natives[] = {
	{ "host_get", (void *)wamr_host_get, "(ii)i", nullptr },
	{ "host_set", (void *)wamr_host_set, "(iii)", nullptr },
};

bool wamr_init() {
	if (g_wamr_ready) {
		return true;
	}
	RuntimeInitArgs init;
	memset(&init, 0, sizeof(init));
	init.mem_alloc_type = Alloc_With_System_Allocator;
	init.running_mode = Mode_Interp;
	init.native_module_name = "host";
	init.native_symbols = g_natives;
	init.n_native_symbols = sizeof(g_natives) / sizeof(g_natives[0]);
	g_wamr_ready = wasm_runtime_full_init(&init);
	if (!g_wamr_ready) {
		g_error = "wasm_runtime_full_init failed";
	}
	return g_wamr_ready;
}

void riscv_install() {
	static bool done;
	if (done) {
		return;
	}
	done = true;
	Machine::install_syscall_handler(500, [](Machine &m) {
		m.set_result(int64_t(g_get(m.sysarg<int32_t>(0), m.sysarg<int32_t>(1))));
	});
	Machine::install_syscall_handler(501, [](Machine &m) {
		g_set(m.sysarg<int32_t>(0), m.sysarg<int32_t>(1), m.sysarg<int32_t>(2));
	});
}
} // namespace

extern "C" {
RT_API void rt_set_host(rt_get_fn get, rt_set_fn set) {
	g_get = get;
	g_set = set;
}

RT_API const char *rt_error(void) { return g_error.c_str(); }

RT_API int32_t rt_load(int32_t vm, const uint8_t *bytes, uint32_t len) {
	auto in = std::make_unique<Instance>();
	in->vm = vm;
	in->bytes.assign(bytes, bytes + len);
	if (vm == RT_WAMR) {
		if (!wamr_init()) {
			return 0;
		}
		char err[128] = { 0 };
		in->module = wasm_runtime_load(in->bytes.data(), len, err, sizeof(err));
		if (!in->module) {
			g_error = std::string("load: ") + err;
			return 0;
		}
		in->inst = wasm_runtime_instantiate(in->module, 64 * 1024, 64 * 1024, err, sizeof(err));
		if (!in->inst) {
			g_error = std::string("instantiate: ") + err;
			wasm_runtime_unload(in->module);
			return 0;
		}
		in->env = wasm_runtime_create_exec_env(in->inst, 256 * 1024);
	} else {
		riscv_install();
		try {
			in->machine = std::make_unique<Machine>(in->bytes, riscv::MachineOptions<riscv::RISCV64>{ .memory_max = 64u << 20 });
		} catch (const std::exception &e) {
			g_error = std::string("riscv load: ") + e.what();
			return 0;
		}
	}
	int32_t h = g_next++;
	g_instances[h] = std::move(in);
	return h;
}

RT_API void rt_unload(int32_t handle) {
	auto it = g_instances.find(handle);
	if (it == g_instances.end()) {
		return;
	}
	Instance &in = *it->second;
	if (in.vm == RT_WAMR) {
		wasm_runtime_destroy_exec_env(in.env);
		wasm_runtime_deinstantiate(in.inst);
		wasm_runtime_unload(in.module);
	}
	g_instances.erase(it);
}

static int64_t call_once(Instance &in, const char *fn, int32_t arg, int32_t *ok, wasm_function_inst_t wf) {
	*ok = 1;
	if (in.vm == RT_WAMR) {
		uint32_t argv[4] = { uint32_t(arg), 0, 0, 0 };
		uint32_t nparams = wasm_func_get_param_count(wf, in.inst);
		if (!wasm_runtime_call_wasm(in.env, wf, nparams, argv)) {
			*ok = 0;
			g_error = std::string("call: ") + wasm_runtime_get_exception(in.inst);
			return 0;
		}
		uint32_t nres = wasm_func_get_result_count(wf, in.inst);
		wasm_valkind_t kinds[1] = { WASM_I32 };
		if (nres) {
			wasm_func_get_result_types(wf, in.inst, kinds);
		}
		if (nres && kinds[0] == WASM_I64) {
			return int64_t(uint64_t(argv[0]) | (uint64_t(argv[1]) << 32));
		}
		return int64_t(int32_t(argv[0]));
	}
	try {
		auto it = in.addrs.find(fn);
		uint64_t addr = it != in.addrs.end() ? it->second : in.machine->address_of(fn);
		if (addr == 0) {
			*ok = 0;
			g_error = std::string("no symbol ") + fn;
			return 0;
		}
		in.addrs[fn] = addr;
		return int64_t(in.machine->vmcall(addr, arg));
	} catch (const std::exception &e) {
		*ok = 0;
		g_error = std::string("riscv call: ") + e.what();
		return 0;
	}
}

static wasm_function_inst_t lookup(Instance &in, const char *fn, int32_t *ok) {
	if (in.vm != RT_WAMR) {
		return nullptr;
	}
	wasm_function_inst_t wf = wasm_runtime_lookup_function(in.inst, fn);
	if (!wf) {
		*ok = 0;
		g_error = std::string("no export ") + fn;
	}
	return wf;
}

RT_API int64_t rt_call(int32_t handle, const char *fn, int32_t arg, int32_t *ok) {
	auto it = g_instances.find(handle);
	if (it == g_instances.end()) {
		*ok = 0;
		g_error = "bad handle";
		return 0;
	}
	*ok = 1;
	wasm_function_inst_t wf = lookup(*it->second, fn, ok);
	if (!*ok) {
		return 0;
	}
	return call_once(*it->second, fn, arg, ok, wf);
}

RT_API int64_t rt_call_n(int32_t handle, const char *fn, int32_t arg, int32_t count, int32_t *ok) {
	auto it = g_instances.find(handle);
	if (it == g_instances.end()) {
		*ok = 0;
		g_error = "bad handle";
		return 0;
	}
	*ok = 1;
	wasm_function_inst_t wf = lookup(*it->second, fn, ok);
	if (!*ok) {
		return 0;
	}
	auto t0 = std::chrono::steady_clock::now();
	for (int32_t i = 0; i < count; i++) {
		call_once(*it->second, fn, arg, ok, wf);
		if (!*ok) {
			break;
		}
	}
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}
}
