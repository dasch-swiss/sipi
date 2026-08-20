//! The `server` table: request fields and the `server.*` functions,
//! error-shape-faithful to the C++ bindings they replace — `(false, msg)`
//! tuples with the historical message strings (including the `generate_jwt`
//! messages that say `'server.table_to_json(table)'`, which scripts may
//! match on). Divergences are the ADR-0023 table only.

use std::cell::RefCell;
use std::ffi::CStr;
use std::io::Read;
use std::rc::Rc;
use std::sync::OnceLock;
use std::time::{Duration, Instant, SystemTime};

use mlua::{Lua, MultiValue, Table, Value, Variadic};

use crate::limits::Deadline;
use crate::runtime::RequestVm;

use super::{BindingCtx, RequestData, ResponseCookie, ResponseWriter, Upload};

/// `server.http` response bodies are capped: Rust-side allocations live
/// outside the Lua memory limit, so an unbounded fetched body would be a
/// request-amplifiable hole (and it feeds `json_to_table`).
pub const HTTP_BODY_CAP: usize = 16 * 1024 * 1024;

/// `server.http` default total-request timeout when the script passes none.
const HTTP_DEFAULT_TIMEOUT: Duration = Duration::from_millis(2000);

type Resp = Rc<RefCell<ResponseWriter>>;

/// Builds the `server` table (fields + functions) for one request.
pub fn install(vm: &RequestVm, ctx: &BindingCtx) -> mlua::Result<Table> {
    let lua = vm.lua();
    let server = lua.create_table()?;
    install_fields(lua, &server, &ctx.request)?;

    let req = Rc::clone(&ctx.request);
    let resp = Rc::clone(&ctx.response);
    let deadline = vm.deadline().clone();

    // ── filesystem table (no chdir — dropped divergence) ────────────────────
    let fs = lua.create_table()?;
    vm.register_binding("server.fs", &fs, "ftype", fs_ftype)?;
    vm.register_binding("server.fs", &fs, "modtime", fs_modtime)?;
    vm.register_binding("server.fs", &fs, "readdir", fs_readdir)?;
    vm.register_binding("server.fs", &fs, "is_readable", fs_is_readable)?;
    vm.register_binding("server.fs", &fs, "is_writeable", fs_is_writeable)?;
    vm.register_binding("server.fs", &fs, "is_executable", fs_is_executable)?;
    vm.register_binding("server.fs", &fs, "exists", fs_exists)?;
    vm.register_binding("server.fs", &fs, "unlink", fs_unlink)?;
    vm.register_binding("server.fs", &fs, "mkdir", fs_mkdir)?;
    vm.register_binding("server.fs", &fs, "rmdir", fs_rmdir)?;
    vm.register_binding("server.fs", &fs, "getcwd", fs_getcwd)?;
    vm.register_binding("server.fs", &fs, "copyFile", fs_copyfile)?;
    {
        let req = Rc::clone(&req);
        vm.register_binding("server.fs", &fs, "moveFile", move |lua, args| {
            fs_movefile(lua, &req, args)
        })?;
    }
    server.set("fs", fs)?;

    // ── json ────────────────────────────────────────────────────────────────
    vm.register_binding("server", &server, "table_to_json", table_to_json)?;
    vm.register_binding("server", &server, "json_to_table", json_to_table)?;

    // ── uuid / base62 ───────────────────────────────────────────────────────
    vm.register_binding("server", &server, "uuid", uuid_v4)?;
    vm.register_binding("server", &server, "uuid62", uuid62)?;
    vm.register_binding("server", &server, "uuid_to_base62", uuid_to_base62)?;
    vm.register_binding("server", &server, "base62_to_uuid", base62_to_uuid)?;

    // ── response writers ────────────────────────────────────────────────────
    {
        let resp = Rc::clone(&resp);
        vm.register_binding("server", &server, "print", move |lua, args| {
            print(lua, &resp, args)
        })?;
    }
    {
        let resp = Rc::clone(&resp);
        vm.register_binding("server", &server, "setBuffer", move |lua, args| {
            set_buffer(lua, &resp, args)
        })?;
    }
    {
        let resp = Rc::clone(&resp);
        vm.register_binding("server", &server, "sendHeader", move |lua, args| {
            send_header(lua, &resp, args)
        })?;
    }
    {
        let resp = Rc::clone(&resp);
        vm.register_binding("server", &server, "sendCookie", move |lua, args| {
            send_cookie(lua, &resp, args)
        })?;
    }
    {
        let resp = Rc::clone(&resp);
        vm.register_binding(
            "server",
            &server,
            "sendStatus",
            move |_, status: Option<i64>| {
                resp.borrow_mut()
                    .set_status(status.unwrap_or(200).clamp(0, 999) as u16);
                Ok(())
            },
        )?;
    }

    // ── request helpers ─────────────────────────────────────────────────────
    {
        let req = Rc::clone(&req);
        vm.register_binding("server", &server, "copyTmpfile", move |lua, args| {
            copy_tmpfile(lua, &req, args)
        })?;
    }
    {
        let req = Rc::clone(&req);
        vm.register_binding("server", &server, "requireAuth", move |lua, (): ()| {
            require_auth(lua, &req)
        })?;
    }

    // ── jwt ─────────────────────────────────────────────────────────────────
    {
        let req = Rc::clone(&req);
        vm.register_binding("server", &server, "generate_jwt", move |lua, args| {
            generate_jwt(lua, &req, args)
        })?;
    }
    {
        let req = Rc::clone(&req);
        vm.register_binding("server", &server, "decode_jwt", move |lua, args| {
            decode_jwt(lua, &req, args)
        })?;
    }

    // ── outbound http ───────────────────────────────────────────────────────
    {
        let req = Rc::clone(&req);
        vm.register_binding("server", &server, "http", move |lua, args| {
            http_client(lua, &req, &deadline, args)
        })?;
    }

    // Deliberate parity gap: there is NO `server.send_error` binding — it
    // never existed in the C++ bindings either (scripts use the `send_error`
    // global from send_response.lua). Pinned by a negative regression test;
    // do not "complete the pattern" by adding one here.

    // ── misc ────────────────────────────────────────────────────────────────
    vm.register_binding("server", &server, "systime", |_, (): ()| {
        Ok(SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map_or(0, |d| d.as_secs() as i64))
    })?;
    vm.register_binding("server", &server, "log", log_message)?;
    vm.register_binding("server", &server, "parse_mimetype", parse_mimetype)?;

    // loglevel constants (ascending severity, matching the C++ LogLevel enum).
    let loglevel = lua.create_table()?;
    for (name, level) in [
        ("LOG_DEBUG", 0),
        ("LOG_INFO", 1),
        ("LOG_NOTICE", 2),
        ("LOG_WARNING", 3),
        ("LOG_ERR", 4),
        ("LOG_CRIT", 5),
        ("LOG_ALERT", 6),
        ("LOG_EMERG", 7),
    ] {
        loglevel.set(name, level)?;
    }
    server.set("loglevel", loglevel)?;

    Ok(server)
}

/// The request-derived `server` fields. `get`/`post`/`request`/`uploads` and
/// `content`/`content_type` are omitted entirely when empty — scripts branch
/// on field presence.
fn install_fields(lua: &Lua, server: &Table, req: &RequestData) -> mlua::Result<()> {
    server.set("method", req.method.as_str())?;
    server.set("has_openssl", true)?;
    server.set("client_ip", req.client_ip.as_str())?;
    server.set("client_port", req.client_port)?;
    server.set("secure", req.secure)?;
    server.set("host", req.host.as_str())?;
    server.set("uri", req.uri.as_str())?;

    let headers = lua.create_table()?;
    for (name, value) in &req.headers {
        headers.set(name.as_str(), value.as_str())?;
    }
    server.set("header", headers)?;

    let cookies = lua.create_table()?;
    for (name, value) in &req.cookies {
        cookies.set(name.as_str(), value.as_str())?;
    }
    server.set("cookies", cookies)?;

    for (field, params) in [
        ("get", &req.get_params),
        ("post", &req.post_params),
        ("request", &req.request_params),
    ] {
        if !params.is_empty() {
            let t = lua.create_table()?;
            for (k, v) in params {
                t.set(k.as_str(), v.as_str())?;
            }
            server.set(field, t)?;
        }
    }

    if !req.uploads.is_empty() {
        let uploads = lua.create_table()?;
        for (i, u) in req.uploads.iter().enumerate() {
            let entry = lua.create_table()?;
            entry.set("fieldname", u.fieldname.as_str())?;
            entry.set("origname", u.origname.as_str())?;
            entry.set("tmpname", u.tmpname.as_str())?;
            entry.set("mimetype", u.mimetype.as_str())?;
            entry.set("filesize", u.filesize)?;
            uploads.set(i + 1, entry)?;
        }
        server.set("uploads", uploads)?;
    }

    if !req.content.is_empty() {
        server.set("content", lua.create_string(&req.content)?)?;
        server.set("content_type", req.content_type.as_str())?;
    }

    if let Some(docroot) = &req.docroot {
        server.set("docroot", docroot.as_str())?;
    }
    Ok(())
}

// ── result-shape helpers ─────────────────────────────────────────────────────
// Every C++ `server.*` function returns `(true, value)` / `(false, msg)`.

fn ok2(_lua: &Lua, value: Value) -> mlua::Result<MultiValue> {
    Ok(MultiValue::from_iter([Value::Boolean(true), value]))
}

fn ok_nil(_lua: &Lua) -> mlua::Result<MultiValue> {
    Ok(MultiValue::from_iter([Value::Boolean(true), Value::Nil]))
}

fn fail(lua: &Lua, msg: impl AsRef<str>) -> mlua::Result<MultiValue> {
    Ok(MultiValue::from_iter([
        Value::Boolean(false),
        Value::String(lua.create_string(msg.as_ref())?),
    ]))
}

/// `strerror(errno)` — the C++ fs bindings surface the bare libc message
/// (no Rust "(os error N)" suffix).
fn strerror(e: &std::io::Error) -> String {
    if let Some(code) = e.raw_os_error() {
        // SAFETY: strerror returns a pointer to a NUL-terminated static /
        // thread-local buffer valid until the next strerror call on this
        // thread; it is copied immediately.
        let msg = unsafe { CStr::from_ptr(libc::strerror(code)) };
        return msg.to_string_lossy().into_owned();
    }
    e.to_string()
}

/// The string-or-1-based-upload-index parameter shape shared by
/// `server.fs.moveFile`, `server.file_mimetype`, and friends.
fn upload_tmpname(req: &RequestData, index: i64) -> Option<&Upload> {
    usize::try_from(index - 1)
        .ok()
        .and_then(|i| req.uploads.get(i))
}

// ── filesystem ───────────────────────────────────────────────────────────────

fn string_arg(
    lua: &Lua,
    args: &Variadic<Value>,
    missing: &str,
    not_string: &str,
) -> Result<Option<String>, mlua::Result<MultiValue>> {
    match args.first() {
        None => Err(fail(lua, missing)),
        Some(Value::String(s)) => Ok(Some(s.to_string_lossy())),
        Some(Value::Integer(i)) => Ok(Some(i.to_string())),
        Some(Value::Number(n)) => Ok(Some(n.to_string())),
        Some(_) => Err(fail(lua, not_string)),
    }
}

macro_rules! try_arg {
    ($e:expr) => {
        match $e {
            Ok(v) => v,
            Err(ret) => return ret,
        }
    };
}

fn fs_ftype(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let path = try_arg!(string_arg(
        lua,
        &args,
        "'server.fs.ftype()': parameter missing",
        "'server.fs.ftype()': filename is not a string"
    ))
    .expect("checked");
    match std::fs::metadata(&path) {
        Err(e) => fail(lua, strerror(&e)),
        Ok(meta) => {
            use std::os::unix::fs::FileTypeExt;
            let ft = meta.file_type();
            let name = if ft.is_file() {
                "FILE"
            } else if ft.is_dir() {
                "DIRECTORY"
            } else if ft.is_char_device() {
                "CHARDEV"
            } else if ft.is_block_device() {
                "BLOCKDEV"
            } else if ft.is_symlink() {
                "LINK"
            } else if ft.is_fifo() {
                "FIFO"
            } else if ft.is_socket() {
                "SOCKET"
            } else {
                "UNKNOWN"
            };
            ok2(lua, Value::String(lua.create_string(name)?))
        }
    }
}

fn fs_modtime(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let path = try_arg!(string_arg(
        lua,
        &args,
        "'server.fs.modtime()': parameter missing",
        "'server.fs.modtime()': filename is not a string"
    ))
    .expect("checked");
    match std::fs::metadata(&path) {
        Err(e) => fail(lua, strerror(&e)),
        Ok(meta) => {
            use std::os::unix::fs::MetadataExt;
            ok2(lua, Value::Integer(meta.mtime()))
        }
    }
}

fn fs_readdir(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let path = try_arg!(string_arg(
        lua,
        &args,
        "'server.fs.readdir()': parameter missing",
        "'server.fs.readdir()': path is not a string"
    ))
    .expect("checked");
    let entries = match std::fs::read_dir(&path) {
        Err(e) => return fail(lua, format!("{}: {path}", strerror(&e))),
        Ok(rd) => rd,
    };
    let out = lua.create_table()?;
    let mut i = 1;
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        // Dotfiles are filtered (the historical prefix compare skipped every
        // name starting with '.').
        if name.starts_with('.') {
            continue;
        }
        out.set(i, name)?;
        i += 1;
    }
    ok2(lua, Value::Table(out))
}

macro_rules! fs_access_probe {
    ($fname:ident, $lua_name:literal, $probe:expr) => {
        fn $fname(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
            let path = try_arg!(string_arg(
                lua,
                &args,
                concat!("'server.fs.", $lua_name, "(filename)': parameter missing"),
                concat!(
                    "'server.fs.",
                    $lua_name,
                    "(filename)': filename is not a string"
                )
            ))
            .expect("checked");
            #[allow(clippy::redundant_closure_call)]
            let yes: bool = ($probe)(&path);
            ok2(lua, Value::Boolean(yes))
        }
    };
}

fn access_ok(path: &str, mode: libc::c_int) -> bool {
    let Ok(c) = std::ffi::CString::new(path) else {
        return false;
    };
    // SAFETY: c is a valid NUL-terminated string for the duration of the call.
    unsafe { libc::access(c.as_ptr(), mode) == 0 }
}

fs_access_probe!(fs_is_readable, "is_readable", |p: &str| access_ok(
    p,
    libc::R_OK
));
fs_access_probe!(fs_is_writeable, "is_writeable", |p: &str| access_ok(
    p,
    libc::W_OK
));
fs_access_probe!(fs_is_executable, "is_executable", |p: &str| access_ok(
    p,
    libc::X_OK
));
fs_access_probe!(fs_exists, "exists", |p: &str| access_ok(p, libc::F_OK));

fn fs_unlink(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let path = try_arg!(string_arg(
        lua,
        &args,
        "'server.fs.unlink(filename)': parameter missing",
        "'server.fs.unlink(filename)': filename is not a string"
    ))
    .expect("checked");
    match std::fs::remove_file(&path) {
        Err(e) => fail(lua, format!("{} File to unlink: {path}", strerror(&e))),
        Ok(()) => ok_nil(lua),
    }
}

fn fs_mkdir(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() < 2 {
        return fail(lua, "'server.fs.mkdir(dirname, mask)': parameter missing");
    }
    let Some(Value::String(dir)) = args.first() else {
        return fail(
            lua,
            "'server.fs.mkdir(dirname, mask)': dirname is not a string",
        );
    };
    let Some(Value::Integer(mode)) = args.get(1) else {
        return fail(
            lua,
            "'server.fs.mkdir(dirname, mask)': mask is not an integer",
        );
    };
    use std::os::unix::fs::DirBuilderExt;
    // The mode is interpreted as the raw integer the script passed —
    // dsp-api passes decimal 511 (= 0o777); that interpretation is pinned.
    let result = std::fs::DirBuilder::new()
        .mode(*mode as u32)
        .create(dir.to_string_lossy());
    match result {
        Err(e) => fail(lua, strerror(&e)),
        Ok(()) => ok_nil(lua),
    }
}

fn fs_rmdir(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let path = try_arg!(string_arg(
        lua,
        &args,
        "'server.fs.rmdir(dirname)': parameter missing",
        "'server.fs.rmdir(dirname)': dirname is not a string"
    ))
    .expect("checked");
    match std::fs::remove_dir(&path) {
        Err(e) => fail(lua, strerror(&e)),
        Ok(()) => ok_nil(lua),
    }
}

fn fs_getcwd(lua: &Lua, (): ()) -> mlua::Result<MultiValue> {
    match std::env::current_dir() {
        Err(e) => fail(lua, strerror(&e)),
        Ok(dir) => ok2(
            lua,
            Value::String(lua.create_string(dir.to_string_lossy().as_bytes())?),
        ),
    }
}

fn fs_copyfile(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() < 2 {
        return fail(lua, "'lua_fs_copyfile(from,to)': not enough parameters");
    }
    let (Some(Value::String(from)), Some(Value::String(to))) = (args.first(), args.get(1)) else {
        return fail(lua, "'lua_fs_copyfile(from,to)': Couldn't open source file");
    };
    copy_file_impl(
        lua,
        &from.to_string_lossy(),
        &to.to_string_lossy(),
        "lua_fs_copyfile",
    )
}

fn copy_file_impl(lua: &Lua, from: &str, to: &str, who: &str) -> mlua::Result<MultiValue> {
    let mut source = match std::fs::File::open(from) {
        Err(_) => {
            let noun = if who == "lua_copytmpfile" {
                "input"
            } else {
                "source"
            };
            return fail(lua, format!("'{who}(from,to)': Couldn't open {noun} file"));
        }
        Ok(f) => f,
    };
    let mut dest = match std::fs::File::create(to) {
        Err(_) => return fail(lua, format!("'{who}(from,to)': Couldn't open output file")),
        Ok(f) => f,
    };
    if std::io::copy(&mut source, &mut dest).is_err() {
        return fail(lua, format!("'{who}(from,to)': Copying data failed"));
    }
    ok_nil(lua)
}

fn fs_movefile(lua: &Lua, req: &RequestData, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() < 2 {
        return fail(lua, "'server.fs.moveFile(from,to)': not enough parameters");
    }
    let from = match args.first() {
        Some(Value::Integer(i)) => match upload_tmpname(req, *i) {
            Some(u) => u.tmpname.clone(),
            None => {
                return fail(
                    lua,
                    "'server.fs.moveFile(from,to)': Could not read data of uploaded file. Invalid index?",
                );
            }
        },
        Some(Value::String(s)) => s.to_string_lossy(),
        _ => {
            return fail(
                lua,
                "server.fs.moveFile(from,to): filename must be string or index",
            )
        }
    };
    let Some(Value::String(to)) = args.get(1) else {
        return fail(lua, "'server.fs.moveFile(from,to)': error moving file!");
    };
    match std::fs::rename(&from, to.to_string_lossy()) {
        Ok(()) => ok_nil(lua),
        Err(e) => match e.raw_os_error() {
            Some(code) if code == libc::EACCES => {
                fail(lua, "'server.fs.moveFile(from,to)': no permission!")
            }
            Some(code) if code == libc::EXDEV => fail(
                lua,
                "'server.fs.moveFile(from,to)': move across file systems not allowd!",
            ),
            _ => fail(lua, "'server.fs.moveFile(from,to)': error moving file!"),
        },
    }
}

// ── response writers ─────────────────────────────────────────────────────────

fn print(lua: &Lua, resp: &Resp, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let mut writer = resp.borrow_mut();
    for arg in args.iter() {
        // lua_tolstring semantics: strings and numbers render; everything
        // else is silently skipped.
        let bytes: Vec<u8> = match arg {
            Value::String(s) => s.as_bytes().to_vec(),
            Value::Integer(i) => i.to_string().into_bytes(),
            Value::Number(n) => n.to_string().into_bytes(),
            _ => continue,
        };
        if writer.write(&bytes).is_err() {
            return fail(lua, "Sending data to connection failed");
        }
    }
    ok_nil(lua)
}

fn set_buffer(lua: &Lua, _resp: &Resp, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    // Framing is the transport's call (the shell streams), so this is a
    // validated no-op, exactly like the C++ FFI sink.
    if let Some(v) = args.first() {
        if !matches!(v, Value::Integer(_)) {
            return fail(
                lua,
                "'server.setbuffer([bufize][, incsize])': requires bufsize size as integer",
            );
        }
    }
    if let Some(v) = args.get(1) {
        if !matches!(v, Value::Integer(_)) {
            return fail(
                lua,
                "'server.setbuffer([bufize][, incsize])': requires incsize size as integer",
            );
        }
    }
    ok_nil(lua)
}

fn send_header(lua: &Lua, resp: &Resp, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() != 2 {
        return fail(
            lua,
            "'server.sendHeader(key,val)': Invalid number of parameters",
        );
    }
    let (name, value) = (coerce_string(&args[0]), coerce_string(&args[1]));
    let (Some(name), Some(value)) = (name, value) else {
        return fail(
            lua,
            "'server.sendHeader(key,val)': Invalid number of parameters",
        );
    };
    resp.borrow_mut().add_header(name, value);
    ok_nil(lua)
}

fn coerce_string(v: &Value) -> Option<String> {
    match v {
        Value::String(s) => Some(s.to_string_lossy()),
        Value::Integer(i) => Some(i.to_string()),
        Value::Number(n) => Some(n.to_string()),
        _ => None,
    }
}

fn send_cookie(lua: &Lua, resp: &Resp, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() < 2 || args.len() > 3 {
        return fail(
            lua,
            "'server.sendCookie(name, value[, options])': Invalid number of parameters",
        );
    }
    let (Some(name), Some(value)) = (coerce_string(&args[0]), coerce_string(&args[1])) else {
        return fail(
            lua,
            "'server.sendCookie(name, value[, options])': Invalid number of parameters",
        );
    };
    let mut cookie = ResponseCookie::new(name, value);
    if let Some(third) = args.get(2) {
        let Value::Table(options) = third else {
            return fail(
                lua,
                "'server.sendCookie(name, value[, options])': options is not a lua-table",
            );
        };
        for pair in options.pairs::<Value, Value>() {
            let (key, val) = pair?;
            let Some(optname) = coerce_string(&key) else {
                return fail(
                    lua,
                    "'server.sendCookie(name, value[, options])': option name is not a string",
                );
            };
            match optname.as_str() {
                "path" => match coerce_string(&val) {
                    Some(p) => cookie.path = p,
                    None => {
                        return fail(
                            lua,
                            "'server.sendCookie(name, value[, options])': path is not string",
                        );
                    }
                },
                "domain" => match coerce_string(&val) {
                    Some(d) => cookie.domain = d,
                    None => {
                        return fail(
                            lua,
                            "'server.sendCookie(name, value[, options])': domain is not string",
                        );
                    }
                },
                "expires" => match val {
                    Value::Integer(secs) => cookie.expires_seconds = Some(secs),
                    _ => {
                        return fail(
                            lua,
                            "'server.sendCookie(name, value[, options])': expires is not integer",
                        );
                    }
                },
                // The boolean options only ever turn a flag ON — a `false`
                // value is a no-op (pinned quirk: `secure=false` cannot clear
                // the secure default).
                "secure" => match val {
                    Value::Boolean(b) => {
                        if b {
                            cookie.secure = true;
                        }
                    }
                    _ => {
                        return fail(
                            lua,
                            "'server.sendCookie(name, value[, options])': secure is not boolean",
                        );
                    }
                },
                "http_only" => match val {
                    Value::Boolean(b) => {
                        if b {
                            cookie.http_only = true;
                        }
                    }
                    _ => {
                        return fail(
                            lua,
                            "'server.sendCookie(name, value[, options])': http_only is not boolean",
                        );
                    }
                },
                other => {
                    return fail(
                        lua,
                        format!(
                            "'server.sendCookie(name, value[, options])': unknown option: {other}"
                        ),
                    );
                }
            }
        }
    }
    resp.borrow_mut().add_cookie(cookie);
    ok_nil(lua)
}

// ── request helpers ──────────────────────────────────────────────────────────

fn copy_tmpfile(lua: &Lua, req: &RequestData, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() < 2 {
        return fail(lua, "'lua_copytmpfile(from,to)': not enough parameters");
    }
    let Some(Value::Integer(index)) = args.first() else {
        return fail(
            lua,
            "'lua_copytmpfile(from,to)': parameter 'from' must be an integer",
        );
    };
    let Some(upload) = upload_tmpname(req, *index) else {
        return fail(
            lua,
            "'lua_copytmpfile(from,to)': parameter 'from' is not a valid index.",
        );
    };
    let Some(target) = args.get(1).and_then(coerce_string) else {
        return fail(lua, "'lua_copytmpfile(from,to)': Couldn't open output file");
    };
    copy_file_impl(lua, &upload.tmpname, &target, "lua_copytmpfile")
}

fn require_auth(lua: &Lua, req: &RequestData) -> mlua::Result<MultiValue> {
    use base64::Engine;
    let auth = req
        .headers
        .iter()
        .find(|(name, _)| name == "authorization")
        .map(|(_, value)| value.clone())
        .unwrap_or_default();
    let out = lua.create_table()?;
    if auth.is_empty() {
        out.set("status", "NOAUTH")?;
    } else if let Some((scheme, rest)) = auth.split_once(' ') {
        match scheme.to_ascii_lowercase().as_str() {
            "basic" => {
                let decoded = base64::engine::general_purpose::STANDARD
                    .decode(rest.trim())
                    .map(|b| String::from_utf8_lossy(&b).into_owned())
                    .unwrap_or_default();
                if let Some((username, password)) = decoded.split_once(':') {
                    out.set("status", "BASIC")?;
                    out.set("username", username)?;
                    out.set("password", password)?;
                } else {
                    out.set("status", "ERROR")?;
                    out.set("message", "Auth-string not valid")?;
                }
            }
            "bearer" => {
                out.set("status", "BEARER")?;
                out.set("token", rest)?;
            }
            // Any other space-separated scheme yields an empty table beyond
            // the outer `true` — the historical (quirky) shape.
            _ => {}
        }
    } else {
        out.set("status", "ERROR")?;
        out.set("message", "Auth-type not known")?;
    }
    ok2(lua, Value::Table(out))
}

// ── json ─────────────────────────────────────────────────────────────────────

/// The C++ `subtable` conversion rules: string keys → object, integer keys →
/// array (in iteration order), mixing is an error, and only
/// number/string/boolean/table values convert.
fn lua_to_json(table: &Table) -> Result<Option<serde_json::Value>, String> {
    let mut object: Option<serde_json::Map<String, serde_json::Value>> = None;
    let mut array: Option<Vec<serde_json::Value>> = None;

    for pair in table.pairs::<Value, Value>() {
        let (key, value) = pair.map_err(|e| e.to_string())?;
        let object_key = match &key {
            Value::String(s) => {
                if array.is_some() {
                    return Err(
                        "'server.table_to_json(table)': Cannot mix int and strings as key".into(),
                    );
                }
                object.get_or_insert_with(serde_json::Map::new);
                Some(s.to_string_lossy())
            }
            Value::Integer(_) | Value::Number(_) => {
                if object.is_some() {
                    return Err(
                        "'server.table_to_json(table)': Cannot mix int and strings as key".into(),
                    );
                }
                array.get_or_insert_with(Vec::new);
                None
            }
            _ => {
                return Err(
                    "'server.table_to_json(table)': Cannot convert key to JSON object field".into(),
                );
            }
        };
        let converted = match &value {
            Value::Integer(i) => serde_json::Value::from(*i),
            Value::Number(n) => {
                if n.floor() == *n && n.is_finite() {
                    serde_json::Value::from(*n as i64)
                } else {
                    serde_json::Value::from(*n)
                }
            }
            Value::String(s) => serde_json::Value::from(s.to_string_lossy()),
            Value::Boolean(b) => serde_json::Value::from(*b),
            Value::Table(t) => {
                lua_to_json(t)?.unwrap_or(serde_json::Value::Object(serde_json::Map::new()))
            }
            _ => return Err("server.table_to_json(table): datatype inconsistency".into()),
        };
        match object_key {
            Some(k) => {
                object.as_mut().expect("object mode").insert(k, converted);
            }
            None => array.as_mut().expect("array mode").push(converted),
        }
    }

    Ok(match (object, array) {
        (Some(o), _) => Some(serde_json::Value::Object(o)),
        (None, Some(a)) => Some(serde_json::Value::Array(a)),
        (None, None) => None, // empty table: the C++ path rendered lua nil
    })
}

/// Renders with jansson's `JSON_INDENT(3)` shape (3-space indent).
fn dumps_indent3(value: &serde_json::Value) -> String {
    let mut out = Vec::new();
    let formatter = serde_json::ser::PrettyFormatter::with_indent(b"   ");
    let mut ser = serde_json::Serializer::with_formatter(&mut out, formatter);
    serde::Serialize::serialize(value, &mut ser).expect("serializing a Value cannot fail");
    String::from_utf8(out).expect("serde_json output is UTF-8")
}

/// JSON → Lua, jansson-walk-faithful: object keys set as-is, **array keys are
/// 0-based** (the historical `json_array_foreach` index), and `null` values
/// render as Lua `nil` — i.e. the key vanishes.
fn json_to_lua(lua: &Lua, value: &serde_json::Value) -> mlua::Result<Value> {
    Ok(match value {
        serde_json::Value::Null => Value::Nil,
        serde_json::Value::Bool(b) => Value::Boolean(*b),
        serde_json::Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                Value::Integer(i)
            } else {
                Value::Number(n.as_f64().unwrap_or(f64::NAN))
            }
        }
        serde_json::Value::String(s) => Value::String(lua.create_string(s)?),
        serde_json::Value::Array(items) => {
            let t = lua.create_table()?;
            for (i, item) in items.iter().enumerate() {
                let v = json_to_lua(lua, item)?;
                if !matches!(v, Value::Nil) {
                    t.set(i as i64, v)?;
                }
            }
            Value::Table(t)
        }
        serde_json::Value::Object(map) => {
            let t = lua.create_table()?;
            for (k, item) in map {
                let v = json_to_lua(lua, item)?;
                if !matches!(v, Value::Nil) {
                    t.set(k.as_str(), v)?;
                }
            }
            Value::Table(t)
        }
    })
}

fn table_to_json(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let Some(first) = args.first() else {
        return fail(
            lua,
            "'server.table_to_json(table)': table parameter missing",
        );
    };
    let Value::Table(table) = first else {
        return fail(
            lua,
            "'server.table_to_json(table)': table is not a lua-table",
        );
    };
    match lua_to_json(table) {
        Err(msg) => fail(lua, msg),
        // An empty table rendered as lua nil on the C++ path.
        Ok(None) => ok_nil(lua),
        Ok(Some(value)) => ok2(
            lua,
            Value::String(lua.create_string(dumps_indent3(&value))?),
        ),
    }
}

fn json_to_table(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let Some(first) = args.first() else {
        return fail(
            lua,
            "'server.json_to_table(jsonstr)': jsonstr parameter missing",
        );
    };
    let Some(jsonstr) = coerce_string(first) else {
        return fail(
            lua,
            "'server.json_to_table(jsonstr)': jsonstr is not a string",
        );
    };
    let parsed: serde_json::Value = match serde_json::from_str(&jsonstr) {
        Err(e) => {
            return fail(
                lua,
                format!("'server.json_to_table(jsonstr)': Error parsing JSON: {e}"),
            );
        }
        Ok(v) => v,
    };
    if !parsed.is_object() && !parsed.is_array() {
        return fail(
            lua,
            "'server.json_to_table(jsonstr)': Not a valid json string",
        );
    }
    let value = json_to_lua(lua, &parsed)?;
    ok2(lua, value)
}

// ── uuid / base62 ────────────────────────────────────────────────────────────

use super::helpers::SoleUuid;

fn uuid_v4(lua: &Lua, (): ()) -> mlua::Result<MultiValue> {
    ok2(
        lua,
        Value::String(lua.create_string(SoleUuid::new_v4().hyphenated())?),
    )
}

fn uuid62(lua: &Lua, (): ()) -> mlua::Result<MultiValue> {
    ok2(
        lua,
        Value::String(lua.create_string(SoleUuid::new_v4().base62())?),
    )
}

fn uuid_to_base62(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.is_empty() {
        return fail(lua, "'server.uuid_tobase62(uuid)': uuid parameter missing");
    }
    let Some(uuidstr) = coerce_string(&args[0]) else {
        return fail(lua, "'server.uuid_tobase62(uuid)': uuid is not a string");
    };
    match SoleUuid::from_hyphenated(&uuidstr) {
        None => fail(lua, "'server.uuid_tobase62(uuid)': uuid is not valid"),
        Some(u) => ok2(lua, Value::String(lua.create_string(u.base62())?)),
    }
}

fn base62_to_uuid(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.is_empty() {
        return fail(
            lua,
            "'server.base62_to_uuid(uuid62)': uuid62 parameter missing",
        );
    }
    let Some(b62) = coerce_string(&args[0]) else {
        return fail(
            lua,
            "'server.base62_to_uuid(uuid62)': uuid62 is not a string",
        );
    };
    match SoleUuid::from_base62(&b62) {
        None => fail(lua, "'server.base62_to_uuid(uuid62)': uuid62 is not valid"),
        Some(u) => ok2(lua, Value::String(lua.create_string(u.hyphenated())?)),
    }
}

// ── jwt ──────────────────────────────────────────────────────────────────────

fn generate_jwt(lua: &Lua, req: &RequestData, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    // The parameter-error messages historically say 'server.table_to_json(table)'
    // (a preserved copy-paste leftover scripts may match on).
    let Some(first) = args.first() else {
        return fail(
            lua,
            "'server.table_to_json(table)': table parameter missing",
        );
    };
    let Value::Table(table) = first else {
        return fail(
            lua,
            "'server.table_to_json(table)': table is not a lua-table",
        );
    };
    let claims = match lua_to_json(table) {
        Err(msg) => return fail(lua, msg),
        Ok(v) => v.unwrap_or(serde_json::Value::Object(serde_json::Map::new())),
    };
    let key = jsonwebtoken::EncodingKey::from_secret(req.jwt_secret.as_bytes());
    match jsonwebtoken::encode(&jsonwebtoken::Header::default(), &claims, &key) {
        Err(_) => fail(lua, "'server.generate_jwt(table)': Encoding token failed"),
        Ok(token) => ok2(lua, Value::String(lua.create_string(token)?)),
    }
}

fn decode_jwt(lua: &Lua, req: &RequestData, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() != 1 {
        return fail(lua, "'server.decode_jwt(token)': error in parameter list");
    }
    let Some(token) = coerce_string(&args[0]) else {
        return fail(
            lua,
            "'server.decode_jwt(token)': error in parameter list: is not string",
        );
    };
    // Hardened validation (ADR-0023 divergence): the algorithm list is pinned
    // to HS256 (an `alg: none` or RS* token is rejected) and `exp` is
    // required and validated (default leeway). Audience is NOT validated here:
    // the host has no expected audience, and jsonwebtoken's validate_aud
    // default would reject every token that merely carries an `aud` claim
    // (dsp-api's all do) — aud/iss policy stays with the script, as before.
    let key = jsonwebtoken::DecodingKey::from_secret(req.jwt_secret.as_bytes());
    let mut validation = jsonwebtoken::Validation::new(jsonwebtoken::Algorithm::HS256);
    validation.validate_aud = false;
    match jsonwebtoken::decode::<serde_json::Value>(&token, &key, &validation) {
        Err(e) => fail(
            lua,
            format!("'server.decode_jwt(token)': Invalid token: {e}"),
        ),
        Ok(data) => {
            let claims = json_to_lua(lua, &data.claims)?;
            ok2(lua, claims)
        }
    }
}

// ── outbound http ────────────────────────────────────────────────────────────

fn http_shared_client() -> Result<&'static reqwest::blocking::Client, String> {
    static CLIENT: OnceLock<Result<reqwest::blocking::Client, String>> = OnceLock::new();
    CLIENT
        .get_or_init(|| {
            reqwest::blocking::Client::builder()
                // Redirects are deliberately not followed (ADR-0023): a
                // redirect chain is SSRF amplification the script never asked
                // for. The single production caller does a direct internal GET.
                .redirect(reqwest::redirect::Policy::none())
                .build()
                .map_err(|e| e.to_string())
        })
        .as_ref()
        .map_err(|e| e.clone())
}

fn http_client(
    lua: &Lua,
    req: &RequestData,
    deadline: &Deadline,
    args: Variadic<Value>,
) -> mlua::Result<MultiValue> {
    if args.len() < 2 {
        return fail(
            lua,
            "'server.http(method, url [, header] [, timeout])' requires at least 2 parameters",
        );
    }
    let method = coerce_string(&args[0]).unwrap_or_default();
    let url = coerce_string(&args[1]).unwrap_or_default();
    if method != "GET" {
        return fail(
            lua,
            format!("'server.http(method, url, [header])': unknown method {method}"),
        );
    }

    let mut headers: Vec<(String, String)> = Vec::new();
    let mut timeout = HTTP_DEFAULT_TIMEOUT;
    for arg in args.iter().skip(2) {
        match arg {
            Value::Table(t) => {
                for pair in t.pairs::<Value, Value>() {
                    let (k, v) = pair?;
                    if let (Some(k), Some(v)) = (coerce_string(&k), coerce_string(&v)) {
                        headers.push((k, v));
                    }
                }
            }
            Value::Integer(ms) => {
                timeout = Duration::from_millis((*ms).max(0) as u64);
            }
            _ => {}
        }
    }
    // Total-request timeout (ADR-0023 divergence: was connect-only), bounded
    // by what is left of the VM deadline so a slow upstream cannot outlive
    // the request budget.
    let timeout = timeout.min(deadline.remaining());

    let client = match http_shared_client() {
        Ok(c) => c,
        Err(e) => return fail(lua, format!("'server.http(...)': {e}")),
    };
    let mut builder = client.get(&url).timeout(timeout);
    let mut has_traceparent = false;
    for (name, value) in &headers {
        if name.eq_ignore_ascii_case("traceparent") {
            has_traceparent = true;
        }
        builder = builder.header(name, value);
    }
    // Continue the caller's distributed trace (host-side per ADR-0017),
    // unless the script set its own.
    if !has_traceparent {
        if let Some(tp) = &req.traceparent {
            builder = builder.header("traceparent", tp);
        }
    }

    let started = Instant::now();
    let response = match builder.send() {
        Err(e) => {
            return fail(
                lua,
                format!("'server.http(...)': HTTP GET request to {url} failed: {e}"),
            );
        }
        Ok(r) => r,
    };
    let status = response.status().as_u16();
    if let Some(len) = response.content_length() {
        if len > HTTP_BODY_CAP as u64 {
            return fail(
                lua,
                format!("'server.http(...)': response body exceeds the {HTTP_BODY_CAP}-byte cap"),
            );
        }
    }
    let response_headers: Vec<(String, String)> = response
        .headers()
        .iter()
        .map(|(name, value)| {
            (
                name.as_str().to_string(),
                String::from_utf8_lossy(value.as_bytes()).into_owned(),
            )
        })
        .collect();
    // Streamed cap: Content-Length can lie or be absent.
    let mut body = Vec::new();
    let mut limited = response.take(HTTP_BODY_CAP as u64 + 1);
    if let Err(e) = limited.read_to_end(&mut body) {
        return fail(
            lua,
            format!("'server.http(...)': HTTP GET request to {url} failed: {e}"),
        );
    }
    if body.len() > HTTP_BODY_CAP {
        return fail(
            lua,
            format!("'server.http(...)': response body exceeds the {HTTP_BODY_CAP}-byte cap"),
        );
    }
    let duration_ms = started.elapsed().as_millis() as i64;

    let out = lua.create_table()?;
    out.set("status_code", status)?;
    out.set("body", lua.create_string(&body)?)?;
    out.set("duration", duration_ms)?;
    let header_table = lua.create_table()?;
    for (name, value) in response_headers {
        header_table.set(name, value)?;
    }
    out.set("header", header_table)?;
    ok2(lua, Value::Table(out))
}

// ── logging / mimetype ───────────────────────────────────────────────────────

fn log_message(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.is_empty() {
        return fail(lua, "'server.log()': no message given");
    }
    let Some(message) = coerce_string(&args[0]) else {
        return fail(lua, "'server.log()': message is not a string");
    };
    let mut level = 4; // LL_ERR default
    if let Some(second) = args.get(1) {
        let Value::Integer(l) = second else {
            return fail(lua, "'server.log()': level is not integer");
        };
        level = *l;
    }
    if !message.is_empty() {
        // Ascending-severity constants (LL_DEBUG=0 … LL_EMERG=7) → tracing.
        match level {
            i64::MIN..=0 => tracing::debug!(target: "sipi::lua", "{message}"),
            1 | 2 => tracing::info!(target: "sipi::lua", "{message}"),
            3 => tracing::warn!(target: "sipi::lua", "{message}"),
            _ => tracing::error!(target: "sipi::lua", "{message}"),
        }
    }
    ok_nil(lua)
}

/// Content-Type parsing (`type/subtype` + optional `charset=`), pure string
/// logic — the C++ regex `^([^;]+)(;\s*charset="?([^"]+)"?)?$`, lowercased.
fn parse_mimetype(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.is_empty() {
        return fail(lua, "server.parse_mimetype(): no argument given");
    }
    let Some(mimestr) = coerce_string(&args[0]) else {
        return fail(lua, "server.parse_mimetype(): argument is not a string");
    };
    match split_mimetype(&mimestr) {
        None => fail(
            lua,
            format!("server.parse_mimetype() failed: Could not parse MIME type: {mimestr}"),
        ),
        Some((mimetype, charset)) => {
            let out = lua.create_table()?;
            out.set("mimetype", mimetype)?;
            if let Some(charset) = charset {
                out.set("charset", charset)?;
            }
            ok2(lua, Value::Table(out))
        }
    }
}

fn split_mimetype(s: &str) -> Option<(String, Option<String>)> {
    match s.split_once(';') {
        None => {
            if s.is_empty() {
                None
            } else {
                Some((s.to_ascii_lowercase(), None))
            }
        }
        Some((mime, rest)) => {
            if mime.is_empty() {
                return None;
            }
            let rest = rest.trim_start();
            let charset = rest
                .strip_prefix("charset=")
                .or_else(|| {
                    // case-insensitive `charset=`
                    let lower = rest.get(..8)?.to_ascii_lowercase();
                    (lower == "charset=").then(|| &rest[8..])
                })?
                .trim_matches('"');
            if charset.is_empty() {
                return None;
            }
            Some((
                mime.to_ascii_lowercase(),
                Some(charset.to_ascii_lowercase()),
            ))
        }
    }
}
