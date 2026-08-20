//! Pins the mlua ↔ `@lua` external-link contract: mlua's `lua53` bindings
//! resolve `lua_*` symbols at link time from the BCR `lua` 5.3.6 `cc_library`
//! (`mlua-sys` feature `external`; its build script emits nothing).
//!
//! ABI assumptions, verified against the BCR module's build (stock
//! `luaconf.h`, sources compiled as C with only `LUA_USE_MACOSX` /
//! `LUA_USE_LINUX` defined):
//! - error handling is C `longjmp`, not C++ exceptions — what mlua expects
//!   from a non-vendored C Lua;
//! - `LUA_INT_TYPE = LUA_INT_LONGLONG` (i64) and `LUA_FLOAT_TYPE = double`
//!   (f64) — the `luaconf.h` defaults, matching mlua-sys's
//!   `lua_Integer`/`lua_Number` (the integer-precision test below fails if
//!   the widths ever diverge);
//! - `LUA_EXTRASPACE = sizeof(void*)` — the default, which mlua-sys
//!   hardcodes in its `lua_getextraspace` reimplementation;
//! - no `LUA_COMPAT_*` flags — mlua-sys references no compat symbols.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use mlua::{Error, HookTriggers, Lua, LuaOptions, StdLib, VmState};

#[test]
fn vm_create_and_chunk_exec() {
    let lua = Lua::new_with(
        StdLib::STRING | StdLib::TABLE | StdLib::MATH,
        LuaOptions::default(),
    )
    .expect("VM creation over the external-linked @lua");
    let n: i64 = lua.load("return 40 + 2").eval().unwrap();
    assert_eq!(n, 42);
    // 2^53 + 1 survives only if lua_Integer is 64-bit; a 32-bit integer
    // build would overflow to float and lose the low bit.
    let precise: i64 = lua.load("return 9007199254740993").eval().unwrap();
    assert_eq!(precise, 9_007_199_254_740_993);
}

#[test]
fn memory_limit_enforced() {
    let lua = Lua::new_with(StdLib::STRING | StdLib::TABLE, LuaOptions::default()).unwrap();
    lua.set_memory_limit(1 << 20).unwrap();
    let res = lua
        .load(r#"local t = {} for i = 1, 1 << 30 do t[i] = string.rep("x", 64) .. i end"#)
        .exec();
    match res {
        Err(Error::MemoryError(_)) => {}
        other => panic!("expected MemoryError, got {other:?}"),
    }
}

#[test]
fn instruction_hook_fires_and_kills() {
    let lua = Lua::new_with(StdLib::NONE, LuaOptions::default()).unwrap();
    let fires = Arc::new(AtomicUsize::new(0));
    let seen = Arc::clone(&fires);
    lua.set_hook(
        HookTriggers::new().every_nth_instruction(100),
        move |_, _| {
            if seen.fetch_add(1, Ordering::Relaxed) >= 4 {
                return Err(Error::runtime("deadline exceeded"));
            }
            Ok(VmState::Continue)
        },
    )
    .unwrap();
    lua.load("while true do end")
        .exec()
        .expect_err("infinite loop must be killed by the hook");
    assert!(fires.load(Ordering::Relaxed) >= 5);
}

// mlua's callback boundary and `catch_rust_panics(false)` require a panic to
// unwind; under `panic=abort` a binding panic would abort the whole process.
// Nothing in the build sets a panic strategy, so this pins the rustc default.
#[test]
fn panics_unwind() {
    let caught = std::panic::catch_unwind(|| panic!("unwind probe"));
    assert!(caught.is_err());
}
