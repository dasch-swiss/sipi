//! The script-visible binding surface: the `server` table (fields +
//! functions), the `config` table, and the `helper` table, installed into a
//! hardened [`crate::runtime::RequestVm`] through its checked-entry
//! chokepoint. The data types here are the Rust-native view of one request
//! ([`RequestData`]) and its response sink ([`ResponseWriter`]) — the shell
//! populates them from axum types; the bindings never see a transport or FFI
//! type.
//!
//! Deliberate divergences from the C++ bindings (ADR-0023 divergence table):
//! `server.shutdown` and `server.fs.chdir` do not exist, `config` carries no
//! `password`/`adminuser`, `server.cookies` is one entry per cookie with
//! original-case names, `decode_jwt` validates `exp` with the algorithm
//! pinned to HS256, and `server.http` uses a total-request timeout with
//! redirects off and the response body capped.

use std::rc::Rc;

use mlua::Table;

use crate::runtime::RequestVm;

pub mod config;
pub mod helpers;
pub mod server;

/// The binding tables the chokepoint-enumeration check sweeps
/// ([`RequestVm::verify_bindings_checked`]): every function-valued entry in
/// these tables must have registered through `register_binding`.
pub const BINDING_TABLES: &[&str] = &["server", "server.fs", "helper", "os"];

/// An uploaded file as seen by Lua (`server.uploads`, `server.copyTmpfile`).
#[derive(Debug, Clone)]
pub struct Upload {
    pub fieldname: String,
    pub origname: String,
    pub tmpname: String,
    pub mimetype: String,
    pub filesize: u64,
}

/// The request as the bindings see it. Header names arrive lowercased (the
/// pinned `server.header` invariant); cookies keep their original-case names,
/// one entry per cookie (DEV-6119).
#[derive(Debug, Clone, Default)]
pub struct RequestData {
    /// The method name as `server.method` renders it ("GET" … "OTHER").
    pub method: String,
    pub client_ip: String,
    pub client_port: i64,
    pub secure: bool,
    pub host: String,
    pub uri: String,
    pub headers: Vec<(String, String)>,
    pub cookies: Vec<(String, String)>,
    pub get_params: Vec<(String, String)>,
    pub post_params: Vec<(String, String)>,
    /// The merged get+post view (`server.request`).
    pub request_params: Vec<(String, String)>,
    pub uploads: Vec<Upload>,
    /// Raw request body; `server.content`/`server.content_type` are omitted
    /// entirely when empty (scripts branch on field presence).
    pub content: Vec<u8>,
    pub content_type: String,
    /// Secret for `server.generate_jwt` / `server.decode_jwt`.
    pub jwt_secret: String,
    /// `server.docroot` for a docroot `.lua`/`.elua` script; `None` for a
    /// configured route (the field is absent, not empty).
    pub docroot: Option<String>,
    /// The host-injected W3C traceparent for `server.http` outbound calls
    /// (ADR-0017: host-side, so a script's own header still wins).
    pub traceparent: Option<String>,
}

/// A cookie set by `server.sendCookie`, rendered to `Set-Cookie` at commit.
/// Defaults mirror the C++ `ResponseCookie` (secure by default, not
/// http-only; the boolean options only ever turn a flag *on* — pinned quirk).
#[derive(Debug, Clone)]
pub struct ResponseCookie {
    pub name: String,
    pub value: String,
    pub path: String,
    pub domain: String,
    pub expires_seconds: Option<i64>,
    pub secure: bool,
    pub http_only: bool,
}

impl ResponseCookie {
    fn new(name: String, value: String) -> Self {
        Self {
            name,
            value,
            path: String::new(),
            domain: String::new(),
            expires_seconds: None,
            secure: true,
            http_only: false,
        }
    }

    /// The `Set-Cookie` value, field order matching the C++ renderer.
    pub fn render(&self) -> String {
        let mut s = format!("{}={}", self.name, self.value);
        if !self.path.is_empty() {
            s.push_str("; Path=");
            s.push_str(&self.path);
        }
        if !self.domain.is_empty() {
            s.push_str("; Domain=");
            s.push_str(&self.domain);
        }
        if let Some(seconds) = self.expires_seconds {
            let when = if seconds >= 0 {
                std::time::SystemTime::now() + std::time::Duration::from_secs(seconds as u64)
            } else {
                std::time::SystemTime::now()
                    - std::time::Duration::from_secs(seconds.unsigned_abs())
            };
            s.push_str("; Expires=");
            s.push_str(&httpdate::fmt_http_date(when));
        }
        if self.secure {
            s.push_str("; Secure");
        }
        if self.http_only {
            s.push_str("; HttpOnly");
        }
        s
    }
}

/// Why a body write failed: the client is gone (receiver dropped).
#[derive(Debug)]
pub struct ClientGone;

/// The head-commit closure: called exactly once, with the final status and
/// headers (cookies already rendered in), when the first body byte is written.
pub type CommitFn = Box<dyn FnOnce(u16, Vec<(String, String)>)>;

/// The body-write closure: one call per chunk after commit.
pub type WriteFn = Box<dyn FnMut(&[u8]) -> Result<(), ClientGone>>;

/// Accumulates the response head and streams the body: `sendStatus` /
/// `sendHeader` / `sendCookie` buffer until the first `print`/body write
/// commits the head (cookies render to `Set-Cookie` headers at that point);
/// after commit the head is fixed. The two closures are the seam to the
/// transport — the shell wires them to the streaming sink's oneshot + body
/// channel, tests wire them to buffers.
pub struct ResponseWriter {
    status: u16,
    headers: Vec<(String, String)>,
    cookies: Vec<ResponseCookie>,
    committed: bool,
    commit: Option<CommitFn>,
    write: WriteFn,
}

impl ResponseWriter {
    pub fn new(commit: CommitFn, write: WriteFn) -> Self {
        Self {
            status: 200,
            headers: Vec::new(),
            cookies: Vec::new(),
            committed: false,
            commit: Some(commit),
            write,
        }
    }

    pub fn set_status(&mut self, status: u16) {
        self.status = status;
    }

    pub fn add_header(&mut self, name: String, value: String) {
        self.headers.push((name, value));
    }

    pub fn add_cookie(&mut self, cookie: ResponseCookie) {
        self.cookies.push(cookie);
    }

    /// Body bytes. The first write commits the head (status + headers +
    /// rendered cookies) through the commit closure.
    pub fn write(&mut self, data: &[u8]) -> Result<(), ClientGone> {
        if !self.committed {
            self.committed = true;
            let mut headers = std::mem::take(&mut self.headers);
            for cookie in self.cookies.drain(..) {
                headers.push(("Set-Cookie".to_string(), cookie.render()));
            }
            if let Some(commit) = self.commit.take() {
                commit(self.status, headers);
            }
        }
        (self.write)(data)
    }

    /// Whether the head has been sent (the pre-/post-commit kill boundary).
    pub fn committed(&self) -> bool {
        self.committed
    }

    /// The buffered head of a response whose body never started — the caller
    /// renders it as a complete response. `None` once committed.
    pub fn into_head(self) -> Option<(u16, Vec<(String, String)>)> {
        if self.committed {
            return None;
        }
        let mut headers = self.headers;
        for cookie in self.cookies {
            headers.push(("Set-Cookie".to_string(), cookie.render()));
        }
        Some((self.status, headers))
    }
}

/// The `config` table values (the `sipiConfGlobals` inventory minus the
/// dropped `password`/`adminuser`). Sizes are resolved bytes, as the C++
/// table rendered them.
#[derive(Debug, Clone, Default)]
pub struct ConfigValues {
    pub hostname: String,
    pub port: i64,
    pub sslport: i64,
    pub imgroot: String,
    pub max_temp_file_age: i64,
    pub prefix_as_path: bool,
    pub init_script: String,
    pub cache_dir: String,
    pub cache_size: i64,
    pub jpeg_quality: i64,
    pub thumb_size: String,
    pub cache_n_files: i64,
    pub max_post_size: i64,
    pub tmpdir: String,
    pub scriptdir: String,
    pub knora_path: String,
    pub knora_port: String,
    pub docroot: String,
}

/// Everything the binding installers need for one request.
pub struct BindingCtx {
    pub request: Rc<RequestData>,
    pub response: Rc<std::cell::RefCell<ResponseWriter>>,
    pub config: Rc<ConfigValues>,
}

/// Installs the full script-visible surface (`server`, `config`, `helper`)
/// into the request VM, every function through the chokepoint.
pub fn install(vm: &RequestVm, ctx: &BindingCtx) -> mlua::Result<()> {
    let server: Table = server::install(vm, ctx)?;
    vm.lua().globals().set("server", server)?;
    config::install(vm, &ctx.config)?;
    let helper = vm.lua().create_table()?;
    helpers::install(vm, &helper)?;
    vm.lua().globals().set("helper", helper)?;
    Ok(())
}
