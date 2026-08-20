//! Runtime-profile tests: the whitelist invariants scripts depend on, the
//! limit enforcement (`pcall` included), the restricted `require`, the
//! bytecode cache, and the checked-entry chokepoint.

use std::io::Write;
use std::time::Duration;

use scripting::{config_vm, KillReason, LimitConfig, RuntimeError, ScriptRuntime};

fn runtime_with(script_dir: &std::path::Path, limits: LimitConfig) -> ScriptRuntime {
    ScriptRuntime::new(script_dir.to_path_buf(), limits)
}

fn test_runtime() -> (tempfile::TempDir, ScriptRuntime) {
    let dir = tempfile::tempdir().expect("tempdir");
    let rt = runtime_with(dir.path(), LimitConfig::default());
    (dir, rt)
}

fn eval_bool(rt: &ScriptRuntime, code: &str) -> bool {
    let vm = rt.request_vm().expect("request vm");
    vm.run(|lua| lua.load(code).eval::<bool>()).expect(code)
}

#[test]
fn stdlib_whitelist_and_base_scrub() {
    let (_dir, rt) = test_runtime();
    for probe in [
        "return io == nil",
        "return debug == nil",
        "return dofile == nil",
        "return loadfile == nil",
        "return load == nil",
        "return collectgarbage == nil",
        "return package.loadlib == nil",
        "return package.searchpath == nil",
        "return package.path == ''",
        "return package.cpath == ''",
        "return print ~= nil",
        "return string ~= nil and table ~= nil and math ~= nil and utf8 ~= nil",
    ] {
        assert!(eval_bool(&rt, probe), "{probe}");
    }
}

#[test]
fn os_shim_is_exactly_getenv_clock_date() {
    let (_dir, rt) = test_runtime();
    assert!(eval_bool(
        &rt,
        r#"
        local keys = {}
        for k in pairs(os) do keys[#keys + 1] = k end
        table.sort(keys)
        return #keys == 3 and keys[1] == "clock" and keys[2] == "date" and keys[3] == "getenv"
        "#,
    ));
    assert!(eval_bool(
        &rt,
        "return os.execute == nil and os.remove == nil"
    ));
}

#[test]
fn string_metatable_stays_linked() {
    let (_dir, rt) = test_runtime();
    // Path-traversal guards use `identifier:find` — the method form must work
    // and the metatable must be the `string` table itself.
    assert!(eval_bool(
        &rt,
        r#"return ("abc"):find("b") == 2 and getmetatable("").__index == string"#,
    ));
}

#[test]
fn table_library_stays_mutable() {
    let (_dir, rt) = test_runtime();
    // upload.lua monkey-patches `table.contains`.
    assert!(eval_bool(
        &rt,
        r#"
        table.contains = function(t, v)
            for _, x in ipairs(t) do if x == v then return true end end
            return false
        end
        return table.contains({"a", "b"}, "b")
        "#,
    ));
}

#[test]
fn globals_shared_within_vm_and_fresh_across_vms() {
    let (_dir, rt) = test_runtime();
    let vm = rt.request_vm().expect("vm");
    // Init chunk defines a bare global (the dsp-api inter-module contract)…
    vm.run(|lua| {
        lua.load("marker = 41; function bump() return marker + 1 end")
            .exec()
    })
    .expect("init chunk");
    // …the route/hook chunk in the same VM sees it.
    let seen: i64 = vm
        .run(|lua| lua.load("return bump()").eval())
        .expect("route chunk");
    assert_eq!(seen, 42);
    // A fresh VM does not.
    assert!(eval_bool(&rt, "return marker == nil and bump == nil"));
}

#[test]
fn memory_bomb_is_killed() {
    let dir = tempfile::tempdir().expect("tempdir");
    let rt = runtime_with(
        dir.path(),
        LimitConfig {
            memory_limit: 1 << 20,
            ..LimitConfig::default()
        },
    );
    let vm = rt.request_vm().expect("vm");
    let res = vm.run(|lua| {
        lua.load(r#"local t = {} for i = 1, 1 << 30 do t[i] = string.rep("x", 64) .. i end"#)
            .exec()
    });
    assert_eq!(res, Err(RuntimeError::Killed(KillReason::Memory)));
}

#[test]
fn infinite_loop_is_killed() {
    let dir = tempfile::tempdir().expect("tempdir");
    let rt = runtime_with(
        dir.path(),
        LimitConfig {
            timeout: Duration::from_millis(50),
            ..LimitConfig::default()
        },
    );
    let vm = rt.request_vm().expect("vm");
    let res = vm.run(|lua| lua.load("while true do end").exec());
    assert_eq!(res, Err(RuntimeError::Killed(KillReason::Timeout)));
}

#[test]
fn pcall_trapped_loop_is_killed() {
    let dir = tempfile::tempdir().expect("tempdir");
    let rt = runtime_with(
        dir.path(),
        LimitConfig {
            timeout: Duration::from_millis(50),
            ..LimitConfig::default()
        },
    );
    let vm = rt.request_vm().expect("vm");
    // The trap loop re-enters pcall forever; the re-armed every-instruction
    // hook raises in the unprotected outer frame, which ends the loop.
    let res = vm.run(|lua| {
        lua.load(
            r#"
            while true do
                pcall(function() while true do end end)
            end
            "#,
        )
        .exec()
    });
    assert_eq!(res, Err(RuntimeError::Killed(KillReason::Timeout)));
}

#[test]
fn trapped_kill_that_returns_normally_is_still_a_kill() {
    let dir = tempfile::tempdir().expect("tempdir");
    let rt = runtime_with(
        dir.path(),
        LimitConfig {
            timeout: Duration::from_millis(20),
            ..LimitConfig::default()
        },
    );
    let vm = rt.request_vm().expect("vm");
    std::thread::sleep(Duration::from_millis(30));
    // Fewer than one hook period of instructions, normal return — the
    // expired deadline still classifies the request as killed.
    let res = vm.run(|lua| lua.load("return true").eval::<bool>());
    assert_eq!(res, Err(RuntimeError::Killed(KillReason::Timeout)));
}

#[test]
fn bindings_are_blocked_after_expiry() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

    let dir = tempfile::tempdir().expect("tempdir");
    let rt = runtime_with(
        dir.path(),
        LimitConfig {
            timeout: Duration::from_millis(20),
            ..LimitConfig::default()
        },
    );
    let vm = rt.request_vm().expect("vm");
    let reached = Arc::new(AtomicBool::new(false));
    let probe = Arc::clone(&reached);
    let table = vm.lua().create_table().expect("table");
    vm.register_binding("server", &table, "probe", move |_, (): ()| {
        probe.store(true, Ordering::Relaxed);
        Ok(true)
    })
    .expect("register");
    vm.lua().globals().set("server", table).expect("set");

    std::thread::sleep(Duration::from_millis(30));
    // The call site is only a handful of instructions — the hook has not
    // fired yet, so the chokepoint is the only guard. It must hold.
    let res = vm.run(|lua| lua.load("return server.probe()").eval::<bool>());
    assert_eq!(res, Err(RuntimeError::Killed(KillReason::Timeout)));
    assert!(
        !reached.load(Ordering::Relaxed),
        "binding body ran after expiry"
    );
}

#[test]
fn binding_registry_catches_bypasses() {
    let (_dir, rt) = test_runtime();
    let vm = rt.request_vm().expect("vm");
    let table = vm.lua().create_table().expect("table");
    vm.register_binding("server", &table, "a", |_, (): ()| Ok(1))
        .expect("register a");
    vm.register_binding("server", &table, "b", |_, (): ()| Ok(2))
        .expect("register b");
    vm.lua().globals().set("server", table).expect("set");
    vm.verify_bindings_checked(&["server", "os"])
        .expect("all bindings registered through the chokepoint");

    // Installing a function around the chokepoint must be flagged.
    let bypass = vm.lua().create_function(|_, (): ()| Ok(3)).expect("fn");
    let server: mlua::Table = vm.lua().globals().get("server").expect("server");
    server.set("bypass", bypass).expect("set bypass");
    let err = vm
        .verify_bindings_checked(&["server"])
        .expect_err("bypass must be detected");
    assert!(err.contains("server.bypass"), "{err}");
}

#[test]
fn require_is_restricted_to_script_dir() {
    let dir = tempfile::tempdir().expect("tempdir");
    std::fs::write(
        dir.path().join("helpers.lua"),
        "local m = {}\nfunction m.answer() return 42 end\nreturn m",
    )
    .expect("write module");
    let rt = runtime_with(dir.path(), LimitConfig::default());

    let vm = rt.request_vm().expect("vm");
    let got: i64 = vm
        .run(|lua| lua.load("return require('helpers').answer()").eval())
        .expect("require from script dir");
    assert_eq!(got, 42);

    for denied in [
        "require('../helpers')",
        "require('sub.helpers')",
        "require('sub/helpers')",
        "require('absent')",
    ] {
        let res = vm.run(|lua| lua.load(&*format!("return {denied}")).exec());
        assert!(
            matches!(res, Err(RuntimeError::Script(_))),
            "{denied}: {res:?}"
        );
    }
}

#[test]
fn required_modules_load_from_the_bytecode_cache() {
    let dir = tempfile::tempdir().expect("tempdir");
    std::fs::write(dir.path().join("mod.lua"), "return 7").expect("write module");
    let rt = runtime_with(dir.path(), LimitConfig::default());

    let first = rt.request_vm().expect("vm");
    let a: i64 = first
        .run(|lua| lua.load("return require('mod')").eval())
        .expect("first require");
    assert_eq!(a, 7);
    assert_eq!(rt.cache().misses(), 1);
    assert_eq!(rt.cache().hits(), 0);

    // A fresh VM (fresh package.loaded) re-requires: bytecode, not source.
    let second = rt.request_vm().expect("vm");
    let b: i64 = second
        .run(|lua| lua.load("return require('mod')").eval())
        .expect("second require");
    assert_eq!(b, 7);
    assert_eq!(rt.cache().hits(), 1);
}

#[test]
fn bytecode_cache_invalidates_on_script_edit() {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("mod.lua");
    std::fs::write(&path, "return 1").expect("write");
    let rt = runtime_with(dir.path(), LimitConfig::default());

    let vm = rt.request_vm().expect("vm");
    let v: i64 = vm
        .run(|lua| lua.load("return require('mod')").eval())
        .expect("first");
    assert_eq!(v, 1);

    // Different length forces invalidation even within mtime granularity.
    let mut f = std::fs::File::create(&path).expect("rewrite");
    f.write_all(b"return 22").expect("write");
    drop(f);

    let vm2 = rt.request_vm().expect("vm");
    let v2: i64 = vm2
        .run(|lua| lua.load("return require('mod')").eval())
        .expect("second");
    assert_eq!(v2, 22);
}

#[test]
fn script_errors_keep_the_chunk_name() {
    let (_dir, rt) = test_runtime();
    let vm = rt.request_vm().expect("vm");
    let res = vm.run(|lua| lua.load("error('boom')").set_name("@scripts/x.lua").exec());
    match res {
        Err(RuntimeError::Script(msg)) => {
            assert!(msg.contains("scripts/x.lua"), "{msg}");
            assert!(msg.contains("boom"), "{msg}");
        }
        other => panic!("expected script error, got {other:?}"),
    }
}

#[test]
fn os_shim_date_clock_getenv() {
    let (_dir, rt) = test_runtime();
    let vm = rt.request_vm().expect("vm");

    let iso: String = vm
        .run(|lua| {
            lua.load("return os.date('!%Y-%m-%d %H:%M:%S', 946684800)")
                .eval()
        })
        .expect("date");
    assert_eq!(iso, "2000-01-01 00:00:00");

    assert!(eval_bool(
        &rt,
        r#"
        local t = os.date("!*t", 946684800)
        return t.year == 2000 and t.month == 1 and t.day == 1 and t.hour == 0
           and t.wday == 7 and t.yday == 1 and t.isdst == false
        "#,
    ));

    // Default format is %c (C-locale shape).
    let c_shape: String = vm
        .run(|lua| lua.load("return os.date('!%c', 946684800)").eval())
        .expect("date %c");
    assert_eq!(c_shape, "Sat Jan  1 00:00:00 2000");

    // An unsupported specifier fails loud.
    let res = vm.run(|lua| lua.load("return os.date('%Q')").exec());
    assert!(matches!(res, Err(RuntimeError::Script(_))), "{res:?}");

    assert!(eval_bool(
        &rt,
        "return type(os.clock()) == 'number' and os.clock() >= 0"
    ));

    std::env::set_var("SIPI_SCRIPTING_TEST_ENV", "marker-value");
    assert!(eval_bool(
        &rt,
        "return os.getenv('SIPI_SCRIPTING_TEST_ENV') == 'marker-value' and os.getenv('SIPI_SCRIPTING_TEST_ABSENT') == nil",
    ));
}

#[test]
fn config_vm_is_whitelisted_but_unlimited() {
    let lua = config_vm().expect("config vm");
    let ok: bool = lua
        .load(
            r#"
            return io == nil and debug == nil and load == nil
               and type(os.getenv) == "function" and os.execute == nil
            "#,
        )
        .eval()
        .expect("probe");
    assert!(ok);
    // No memory cap on the trusted startup path: a multi-MiB allocation
    // succeeds where the request profile would have killed it.
    let big: i64 = lua
        .load("local t = {} for i = 1, 200000 do t[i] = i end return #t")
        .eval()
        .expect("alloc");
    assert_eq!(big, 200_000);
}
