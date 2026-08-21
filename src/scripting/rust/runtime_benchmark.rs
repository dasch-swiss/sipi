//! Per-request Lua VM cost, Rust runtime: hardened-profile VM build, init
//! execution through the bytecode cache (`require`d modules included), and
//! the instruction-hook overhead on a fixed compute chunk
//! (`docs/src/development/benchmarking.md` discipline).
//!
//! The init script + script dir default to the repo's own
//! `config/sipi.init.lua` / `scripts/`; point SIPI_BENCH_INITSCRIPT /
//! SIPI_BENCH_SCRIPTDIR at a dsp-api checkout to measure the production
//! closure (9 files, 8 arriving via require).

use std::path::PathBuf;
use std::time::{Duration, Instant};

use scripting::{LimitConfig, ScriptRuntime};

fn env_or(name: &str, fallback: &str) -> String {
    std::env::var(name)
        .ok()
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| fallback.to_string())
}

/// Run `f` repeatedly for ~1s after warmup and report median/mean per call.
fn bench(name: &str, mut f: impl FnMut()) {
    for _ in 0..10 {
        f();
    }
    let mut samples = Vec::new();
    let budget = Instant::now();
    while budget.elapsed() < Duration::from_secs(1) || samples.len() < 30 {
        let t = Instant::now();
        f();
        samples.push(t.elapsed());
        if samples.len() >= 20_000 {
            break;
        }
    }
    samples.sort_unstable();
    let median = samples[samples.len() / 2];
    let mean = samples.iter().sum::<Duration>() / samples.len() as u32;
    println!(
        "{name:<40} median {:>10.3} µs   mean {:>10.3} µs   n {}",
        median.as_secs_f64() * 1e6,
        mean.as_secs_f64() * 1e6,
        samples.len()
    );
}

fn main() {
    let initscript = PathBuf::from(env_or("SIPI_BENCH_INITSCRIPT", "config/sipi.init.lua"));
    let scriptdir = PathBuf::from(env_or("SIPI_BENCH_SCRIPTDIR", "scripts"));
    println!(
        "initscript {}   scriptdir {}",
        initscript.display(),
        scriptdir.display()
    );

    let rt = ScriptRuntime::new(scriptdir, LimitConfig::default());

    // Cold compile once so the timed loops measure the warm-cache path (the
    // steady state every request after the first sees).
    {
        let vm = rt.request_vm().expect("vm");
        let f = rt
            .cache()
            .load_function(vm.lua(), &initscript)
            .expect("init compiles");
        vm.run(|_| f.call::<()>(())).expect("init executes");
    }

    bench("vm_build_only (hardened profile)", || {
        let vm = rt.request_vm().expect("vm");
        std::hint::black_box(vm.lua());
    });

    bench("vm_build + init exec (bytecode cache)", || {
        let vm = rt.request_vm().expect("vm");
        let f = rt
            .cache()
            .load_function(vm.lua(), &initscript)
            .expect("cached init");
        vm.run(|_| f.call::<()>(())).expect("init executes");
    });

    const CHUNK: &str = "local s = 0 for i = 1, 100000 do s = s + i % 7 end return s";

    let hooked = rt.request_vm().expect("vm");
    bench("compute chunk, hook armed (default period)", || {
        let v: i64 = hooked.run(|lua| lua.load(CHUNK).eval()).expect("chunk");
        std::hint::black_box(v);
    });

    // The same chunk with the hook effectively disarmed (max period) — the
    // delta against the armed run is the hook tax on pure Lua execution.
    let unhooked_rt = ScriptRuntime::new(
        PathBuf::from(env_or("SIPI_BENCH_SCRIPTDIR", "scripts")),
        LimitConfig {
            hook_period: u32::MAX,
            ..LimitConfig::default()
        },
    );
    let unhooked = unhooked_rt.request_vm().expect("vm");
    bench("compute chunk, hook period u32::MAX", || {
        let v: i64 = unhooked.run(|lua| lua.load(CHUNK).eval()).expect("chunk");
        std::hint::black_box(v);
    });

    // No hook installed at all (the config-VM profile): isolates the count
    // hook's per-instruction tax — with LUA_MASKCOUNT set the interpreter
    // decrements the hook counter on every instruction regardless of period.
    let nohook = scripting::config_vm().expect("config vm");
    bench("compute chunk, no hook installed", || {
        let v: i64 = nohook.load(CHUNK).eval().expect("chunk");
        std::hint::black_box(v);
    });
}
