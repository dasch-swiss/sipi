//! The `sqlite` binding: `db = sqlite(path[, mode])`, `qry = db << sql`,
//! `row = qry([binds…])`, `~qry` / `~db` — a thin hand-written FFI directly
//! over the BCR `@sqlite3` `cc_library` (the same external-link pattern as
//! mlua ↔ `@lua`; deliberately not `libsqlite3-sys`, whose build script
//! emits `-lsqlite3` link-search directives that would resolve against the
//! system sqlite instead of the Bazel-linked one).
//!
//! Semantics are shape-preserved from the C++ binding: the error convention
//! is real Lua errors (not `(false, msg)` tuples), row tables use **0-based**
//! column keys, binds are 1-based from the second call argument. Deliberate
//! changes: the mode argument works (the C++ code read the path twice, so
//! `"RO"`/`"CRW"` were dead); a `Stmt` holds shared ownership of its
//! connection, so the connection closes only after every statement is gone
//! (the C++ version left dangling statement handles — use-after-close);
//! `~db` marks the connection closed and later statement calls raise a clean
//! `database is closed` error; integer binds/columns are 64-bit.
//!
//! Deadline: the metamethod entries (`__shl`, `__call`, `__bnot`) cannot
//! register through the table chokepoint, so each checks the VM deadline
//! inline at entry; the busy timeout is derived from the remaining deadline
//! at open.

use std::cell::Cell;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::rc::Rc;

use mlua::{AnyUserData, Lua, Table, Value, Variadic};

use crate::limits::Deadline;
use crate::runtime::RequestVm;

#[repr(C)]
struct Sqlite3 {
    _opaque: [u8; 0],
}
#[repr(C)]
struct Sqlite3Stmt {
    _opaque: [u8; 0],
}

const SQLITE_OK: c_int = 0;
const SQLITE_ROW: c_int = 100;
const SQLITE_DONE: c_int = 101;
const SQLITE_OPEN_READONLY: c_int = 0x0000_0001;
const SQLITE_OPEN_READWRITE: c_int = 0x0000_0002;
const SQLITE_OPEN_CREATE: c_int = 0x0000_0004;
const SQLITE_OPEN_NOMUTEX: c_int = 0x0000_8000;
const SQLITE_INTEGER: c_int = 1;
const SQLITE_FLOAT: c_int = 2;
const SQLITE_TEXT: c_int = 3;
const SQLITE_BLOB: c_int = 4;
const SQLITE_NULL: c_int = 5;

extern "C" {
    fn sqlite3_open_v2(
        filename: *const c_char,
        pp_db: *mut *mut Sqlite3,
        flags: c_int,
        z_vfs: *const c_char,
    ) -> c_int;
    fn sqlite3_close_v2(db: *mut Sqlite3) -> c_int;
    fn sqlite3_errmsg(db: *mut Sqlite3) -> *const c_char;
    fn sqlite3_busy_timeout(db: *mut Sqlite3, ms: c_int) -> c_int;
    fn sqlite3_prepare_v2(
        db: *mut Sqlite3,
        sql: *const c_char,
        n_byte: c_int,
        pp_stmt: *mut *mut Sqlite3Stmt,
        pz_tail: *mut *const c_char,
    ) -> c_int;
    fn sqlite3_step(stmt: *mut Sqlite3Stmt) -> c_int;
    fn sqlite3_finalize(stmt: *mut Sqlite3Stmt) -> c_int;
    fn sqlite3_reset(stmt: *mut Sqlite3Stmt) -> c_int;
    fn sqlite3_clear_bindings(stmt: *mut Sqlite3Stmt) -> c_int;
    fn sqlite3_bind_int64(stmt: *mut Sqlite3Stmt, idx: c_int, value: i64) -> c_int;
    fn sqlite3_bind_double(stmt: *mut Sqlite3Stmt, idx: c_int, value: f64) -> c_int;
    fn sqlite3_bind_text(
        stmt: *mut Sqlite3Stmt,
        idx: c_int,
        value: *const c_char,
        n: c_int,
        destructor: *const c_void,
    ) -> c_int;
    fn sqlite3_bind_blob(
        stmt: *mut Sqlite3Stmt,
        idx: c_int,
        value: *const c_void,
        n: c_int,
        destructor: *const c_void,
    ) -> c_int;
    fn sqlite3_bind_null(stmt: *mut Sqlite3Stmt, idx: c_int) -> c_int;
    fn sqlite3_column_count(stmt: *mut Sqlite3Stmt) -> c_int;
    fn sqlite3_column_type(stmt: *mut Sqlite3Stmt, col: c_int) -> c_int;
    fn sqlite3_column_int64(stmt: *mut Sqlite3Stmt, col: c_int) -> i64;
    fn sqlite3_column_double(stmt: *mut Sqlite3Stmt, col: c_int) -> f64;
    fn sqlite3_column_bytes(stmt: *mut Sqlite3Stmt, col: c_int) -> c_int;
    fn sqlite3_column_blob(stmt: *mut Sqlite3Stmt, col: c_int) -> *const c_void;
    fn sqlite3_column_text(stmt: *mut Sqlite3Stmt, col: c_int) -> *const c_char;
    fn sqlite3_sql(stmt: *mut Sqlite3Stmt) -> *const c_char;
}

/// `SQLITE_TRANSIENT`: sqlite copies the buffer before returning.
fn transient() -> *const c_void {
    -1isize as *const c_void
}

/// The shared connection: statements hold an `Rc` of it, so the handle
/// outlives every statement and closes exactly once.
struct DbInner {
    handle: *mut Sqlite3,
    name: String,
    closed: Cell<bool>,
}

impl DbInner {
    fn errmsg(&self) -> String {
        // SAFETY: the handle is live (closed only in Drop) and errmsg returns
        // a NUL-terminated string owned by sqlite, copied immediately.
        unsafe { CStr::from_ptr(sqlite3_errmsg(self.handle)) }
            .to_string_lossy()
            .into_owned()
    }
}

impl Drop for DbInner {
    fn drop(&mut self) {
        // SAFETY: the handle came from sqlite3_open_v2 and every statement
        // holds an Rc of this struct, so all statements finalized before this
        // runs; close_v2 is NULL-safe.
        unsafe { sqlite3_close_v2(self.handle) };
    }
}

/// The `db` userdata payload.
pub struct LuaSqlite {
    inner: Rc<DbInner>,
}

/// The `qry` userdata payload. Keeps the connection alive via the `Rc`.
pub struct LuaStmt {
    db: Rc<DbInner>,
    stmt: Cell<*mut Sqlite3Stmt>,
}

impl Drop for LuaStmt {
    fn drop(&mut self) {
        let stmt = self.stmt.replace(std::ptr::null_mut());
        if !stmt.is_null() {
            // SAFETY: the statement handle is live (nulled on finalize) and
            // its connection is kept alive by the Rc.
            unsafe { sqlite3_finalize(stmt) };
        }
    }
}

fn lua_err(msg: impl std::fmt::Display) -> mlua::Error {
    mlua::Error::runtime(msg.to_string())
}

/// Installs the `sqlite` global + registers the Db/Stmt userdata types.
pub fn install(vm: &RequestVm) -> mlua::Result<()> {
    let lua = vm.lua();
    let deadline = vm.deadline().clone();

    {
        let deadline = deadline.clone();
        let globals: Table = lua.globals();
        vm.register_binding(
            "_G",
            &globals,
            "sqlite",
            move |lua, args: Variadic<Value>| sqlite_new(lua, &deadline, args),
        )?;
    }

    let db_deadline = deadline.clone();
    lua.register_userdata_type::<LuaSqlite>(move |reg| {
        use mlua::UserDataMethods;
        let deadline = db_deadline.clone();
        reg.add_meta_function("__shl", move |lua, (db, sql): (AnyUserData, Value)| {
            deadline.check()?;
            db_query(lua, &db, &sql)
        });
        reg.add_meta_function("__bnot", |_, db: AnyUserData| {
            let this = db.borrow::<LuaSqlite>()?;
            this.inner.closed.set(true);
            Ok(Value::Nil)
        });
        reg.add_meta_method("__tostring", |_, this: &LuaSqlite, (): ()| {
            Ok(format!("DB-File: {}", this.inner.name))
        });
    })?;

    let stmt_deadline = deadline;
    lua.register_userdata_type::<LuaStmt>(move |reg| {
        use mlua::UserDataMethods;
        let deadline = stmt_deadline.clone();
        reg.add_meta_function("__call", move |lua, args: Variadic<Value>| {
            deadline.check()?;
            stmt_next(lua, args)
        });
        reg.add_meta_function("__bnot", |_, stmt: AnyUserData| {
            let this = stmt.borrow::<LuaStmt>()?;
            let handle = this.stmt.replace(std::ptr::null_mut());
            if !handle.is_null() {
                // SAFETY: the handle is live and nulled here exactly once.
                unsafe { sqlite3_finalize(handle) };
            }
            Ok(Value::Nil)
        });
        reg.add_meta_method("__tostring", |_, this: &LuaStmt, (): ()| {
            let stmt = this.stmt.get();
            if stmt.is_null() {
                return Ok("SQL: (finalized)".to_string());
            }
            // SAFETY: the statement handle is live; sqlite3_sql returns a
            // NUL-terminated string owned by the statement, copied here.
            let sql = unsafe { CStr::from_ptr(sqlite3_sql(stmt)) }.to_string_lossy();
            Ok(format!("SQL: {sql}"))
        });
    })?;

    Ok(())
}

fn sqlite_new(lua: &Lua, deadline: &Deadline, args: Variadic<Value>) -> mlua::Result<Value> {
    let Some(Value::String(path)) = args.first() else {
        return Err(lua_err("'sqlite(path, mode)': no enough parameters!"));
    };
    let path = path.to_string_lossy();
    let mut flags = SQLITE_OPEN_READWRITE;
    if let Some(Value::String(mode)) = args.get(1) {
        flags = match mode.to_string_lossy().as_str() {
            "RO" => SQLITE_OPEN_READONLY,
            "RW" => SQLITE_OPEN_READWRITE,
            "CRW" => SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            _ => SQLITE_OPEN_READWRITE,
        };
    }
    flags |= SQLITE_OPEN_NOMUTEX;

    let c_path =
        CString::new(path.as_str()).map_err(|_| lua_err("'sqlite(path, mode)': invalid path"))?;
    let mut handle: *mut Sqlite3 = std::ptr::null_mut();
    // SAFETY: c_path outlives the call; handle receives the connection.
    let status = unsafe { sqlite3_open_v2(c_path.as_ptr(), &mut handle, flags, std::ptr::null()) };
    if status != SQLITE_OK {
        let msg = if handle.is_null() {
            "out of memory".to_string()
        } else {
            // SAFETY: sqlite returns a valid handle even on open failure for
            // error retrieval; closed right after.
            let msg = unsafe { CStr::from_ptr(sqlite3_errmsg(handle)) }
                .to_string_lossy()
                .into_owned();
            // SAFETY: as above.
            unsafe { sqlite3_close_v2(handle) };
            msg
        };
        return Err(lua_err(msg));
    }
    // Blocking budget: a locked database waits at most the remaining VM
    // deadline, never past it.
    let budget_ms = deadline.remaining().as_millis().min(c_int::MAX as u128) as c_int;
    // SAFETY: the handle is live.
    unsafe { sqlite3_busy_timeout(handle, budget_ms) };

    let ud = lua.create_any_userdata(LuaSqlite {
        inner: Rc::new(DbInner {
            handle,
            name: path,
            closed: Cell::new(false),
        }),
    })?;
    Ok(Value::UserData(ud))
}

fn db_query(lua: &Lua, db: &AnyUserData, sql: &Value) -> mlua::Result<Value> {
    let this = db
        .borrow::<LuaSqlite>()
        .map_err(|_| lua_err("Type error"))?;
    if this.inner.closed.get() {
        return Err(lua_err("database is closed"));
    }
    let sql = match sql {
        Value::String(s) => s.to_string_lossy(),
        _ => return Err(lua_err("No SQL given!")),
    };
    let c_sql = CString::new(sql.as_str()).map_err(|_| lua_err("No SQL given!"))?;
    let mut stmt: *mut Sqlite3Stmt = std::ptr::null_mut();
    // SAFETY: the connection is live (not closed); c_sql outlives the call.
    let status = unsafe {
        sqlite3_prepare_v2(
            this.inner.handle,
            c_sql.as_ptr(),
            sql.len() as c_int,
            &mut stmt,
            std::ptr::null_mut(),
        )
    };
    if status != SQLITE_OK {
        return Err(lua_err(this.inner.errmsg()));
    }
    let ud = lua.create_any_userdata(LuaStmt {
        db: Rc::clone(&this.inner),
        stmt: Cell::new(stmt),
    })?;
    Ok(Value::UserData(ud))
}

fn stmt_next(lua: &Lua, args: Variadic<Value>) -> mlua::Result<Value> {
    let Some(Value::UserData(ud)) = args.first() else {
        return Err(lua_err("Stmt_next: Invalid prepared statment!"));
    };
    let this = ud
        .borrow::<LuaStmt>()
        .map_err(|_| lua_err("Stmt_next: Invalid prepared statment!"))?;
    if this.db.closed.get() {
        return Err(lua_err("database is closed"));
    }
    let stmt = this.stmt.get();
    if stmt.is_null() {
        return Err(lua_err("Stmt_next: Invalid prepared statment!"));
    }

    if args.len() > 1 {
        // SAFETY: the statement handle is live for all calls below.
        unsafe {
            if sqlite3_reset(stmt) != SQLITE_OK || sqlite3_clear_bindings(stmt) != SQLITE_OK {
                return Err(lua_err(this.db.errmsg()));
            }
        }
        for (i, value) in args.iter().skip(1).enumerate() {
            let idx = (i + 1) as c_int;
            let status = match value {
                // SAFETY: the statement handle is live.
                Value::Integer(v) => unsafe { sqlite3_bind_int64(stmt, idx, *v) },
                // SAFETY: the statement handle is live.
                Value::Number(v) => unsafe { sqlite3_bind_double(stmt, idx, *v) },
                Value::String(s) => {
                    let bytes = s.as_bytes();
                    if bytes.contains(&0) {
                        // SAFETY: the statement handle is live; SQLITE_TRANSIENT
                        // makes sqlite copy the buffer before returning.
                        unsafe {
                            sqlite3_bind_blob(
                                stmt,
                                idx,
                                bytes.as_ptr() as *const c_void,
                                bytes.len() as c_int,
                                transient(),
                            )
                        }
                    } else {
                        // SAFETY: the statement handle is live; SQLITE_TRANSIENT
                        // makes sqlite copy the buffer before returning.
                        unsafe {
                            sqlite3_bind_text(
                                stmt,
                                idx,
                                bytes.as_ptr() as *const c_char,
                                bytes.len() as c_int,
                                transient(),
                            )
                        }
                    }
                }
                // SAFETY: the statement handle is live.
                Value::Nil => unsafe { sqlite3_bind_null(stmt, idx) },
                // SAFETY: the statement handle is live.
                Value::Boolean(b) => unsafe { sqlite3_bind_int64(stmt, idx, i64::from(*b)) },
                _ => {
                    return Err(lua_err(
                        "Stmt_next: Invalid datatype for binding to prepared statments!",
                    ));
                }
            };
            if status != SQLITE_OK {
                return Err(lua_err(this.db.errmsg()));
            }
        }
    }

    // SAFETY: the statement handle is live.
    let status = unsafe { sqlite3_step(stmt) };
    match status {
        SQLITE_ROW => {
            // SAFETY: SQLITE_ROW guarantees the column accessors below are
            // valid for this statement until the next step/reset.
            let ncols = unsafe { sqlite3_column_count(stmt) };
            let row = lua.create_table()?;
            for col in 0..ncols {
                // Row tables keep the historical 0-based column keys.
                // SAFETY: `col` < column_count; each accessor matches the
                // reported column type.
                unsafe {
                    match sqlite3_column_type(stmt, col) {
                        SQLITE_INTEGER => row.set(col, sqlite3_column_int64(stmt, col))?,
                        SQLITE_FLOAT => row.set(col, sqlite3_column_double(stmt, col))?,
                        SQLITE_BLOB => {
                            let n = sqlite3_column_bytes(stmt, col) as usize;
                            let ptr = sqlite3_column_blob(stmt, col);
                            let bytes = if ptr.is_null() || n == 0 {
                                &[][..]
                            } else {
                                std::slice::from_raw_parts(ptr as *const u8, n)
                            };
                            row.set(col, lua.create_string(bytes)?)?;
                        }
                        SQLITE_TEXT => {
                            let ptr = sqlite3_column_text(stmt, col);
                            let text = if ptr.is_null() {
                                &[][..]
                            } else {
                                CStr::from_ptr(ptr as *const c_char).to_bytes()
                            };
                            row.set(col, lua.create_string(text)?)?;
                        }
                        SQLITE_NULL => {} // nil value: the key vanishes
                        _ => {}
                    }
                }
            }
            Ok(Value::Table(row))
        }
        SQLITE_DONE => Ok(Value::Nil),
        _ => Err(lua_err(this.db.errmsg())),
    }
}
