//! Binding-surface tests: field parity (omit-if-empty, lowercase headers,
//! original-case cookies), result shapes (`(true, v)` / `(false, msg)`),
//! json/uuid/jwt semantics, the requireAuth quirks, the fs table, and the
//! chokepoint enumeration over every binding table.

use std::cell::RefCell;
use std::rc::Rc;

use mlua::{MultiValue, Value};
use scripting::bindings::{self, BindingCtx, ConfigValues, RequestData, ResponseWriter, Upload};
use scripting::{LimitConfig, RequestVm, ScriptRuntime};

type Head = Rc<RefCell<Option<(u16, Vec<(String, String)>)>>>;
type Body = Rc<RefCell<Vec<u8>>>;

fn test_vm(req: RequestData) -> (RequestVm, Head, Body) {
    let dir = std::env::temp_dir();
    let rt = ScriptRuntime::new(dir, LimitConfig::default());
    let vm = rt.request_vm().expect("vm");
    let head: Head = Rc::default();
    let body: Body = Rc::default();
    let commit_head = Rc::clone(&head);
    let write_body = Rc::clone(&body);
    let writer = ResponseWriter::new(
        Box::new(move |status, headers| {
            *commit_head.borrow_mut() = Some((status, headers));
        }),
        Box::new(move |data| {
            write_body.borrow_mut().extend_from_slice(data);
            Ok(())
        }),
    );
    let ctx = BindingCtx {
        request: Rc::new(req),
        response: Rc::new(RefCell::new(writer)),
        config: Rc::new(ConfigValues {
            hostname: "test-host".into(),
            port: 1024,
            sslport: -1,
            imgroot: "./images".into(),
            prefix_as_path: true,
            thumb_size: "!128,128".into(),
            knora_path: "localhost".into(),
            knora_port: "3434".into(),
            ..Default::default()
        }),
    };
    bindings::install(&vm, &ctx).expect("install bindings");
    (vm, head, body)
}

fn sample_request() -> RequestData {
    RequestData {
        method: "POST".into(),
        client_ip: "192.0.2.7".into(),
        client_port: 4711,
        secure: true,
        host: "sipi.example".into(),
        uri: "/api/upload".into(),
        headers: vec![
            ("authorization".into(), "Bearer tok-123".into()),
            ("cookie".into(), "sid=abc; KnoraAuth=xyz".into()),
        ],
        cookies: vec![
            ("sid".into(), "abc".into()),
            ("KnoraAuth".into(), "xyz".into()),
        ],
        get_params: vec![("q".into(), "1".into())],
        post_params: vec![],
        request_params: vec![("q".into(), "1".into())],
        uploads: vec![Upload {
            fieldname: "file".into(),
            origname: "img.tif".into(),
            tmpname: "/tmp/does-not-exist-upload".into(),
            mimetype: "image/tiff".into(),
            filesize: 42,
        }],
        content: b"raw-body".to_vec(),
        content_type: "application/octet-stream".into(),
        jwt_secret: "UP 4888, nice 4-8-4 steam engine".into(),
        docroot: None,
        traceparent: None,
    }
}

fn eval_bool(vm: &RequestVm, code: &str) -> bool {
    vm.run(|lua| lua.load(code).eval::<bool>()).expect(code)
}

fn call2(vm: &RequestVm, code: &str) -> (bool, Value) {
    let mv: MultiValue = vm.run(|lua| lua.load(code).eval()).expect(code);
    let mut it = mv.into_iter();
    let ok = matches!(it.next(), Some(Value::Boolean(true)));
    (ok, it.next().unwrap_or(Value::Nil))
}

fn as_str(v: &Value) -> String {
    match v {
        Value::String(s) => s.to_string_lossy(),
        other => panic!("expected string, got {other:?}"),
    }
}

// ── fields ───────────────────────────────────────────────────────────────────

#[test]
fn server_fields_present_and_shaped() {
    let (vm, _, _) = test_vm(sample_request());
    for probe in [
        "return server.method == 'POST'",
        "return server.has_openssl == true",
        "return server.client_ip == '192.0.2.7'",
        "return server.client_port == 4711",
        "return server.secure == true",
        "return server.host == 'sipi.example'",
        "return server.uri == '/api/upload'",
        // lowercase header keys — the pinned dsp-api JWT-auth invariant.
        "return server.header['authorization'] == 'Bearer tok-123'",
        "return server.header['cookie'] ~= nil",
        // DEV-6119: one entry per cookie, original-case names.
        "return server.cookies['sid'] == 'abc'",
        "return server.cookies['KnoraAuth'] == 'xyz'",
        "return server.get['q'] == '1'",
        "return server.request['q'] == '1'",
        // post had no params → the field is absent, not an empty table.
        "return server.post == nil",
        "return server.content == 'raw-body'",
        "return server.content_type == 'application/octet-stream'",
        "return server.uploads[1].origname == 'img.tif'",
        "return server.uploads[1].filesize == 42",
        // configured route: no docroot field.
        "return server.docroot == nil",
        // dropped divergences: absent, not stubbed.
        "return server.shutdown == nil",
        "return server.fs.chdir == nil",
        // server.send_error never existed in the C++ bindings either —
        // scripts use the send_error global from send_response.lua. Parity
        // here means ABSENT; do not "complete the pattern".
        "return server.send_error == nil",
        // config table minus credentials.
        "return config.hostname == 'test-host' and config.port == 1024",
        "return config.sslport == -1 and config.prefix_as_path == true",
        "return config.password == nil and config.adminuser == nil",
    ] {
        assert!(eval_bool(&vm, probe), "{probe}");
    }
}

#[test]
fn empty_request_omits_conditional_fields() {
    let (vm, _, _) = test_vm(RequestData::default());
    assert!(eval_bool(
        &vm,
        "return server.get == nil and server.post == nil and server.request == nil \
           and server.uploads == nil and server.content == nil and server.content_type == nil",
    ));
}

// ── response writing ─────────────────────────────────────────────────────────

#[test]
fn response_head_commits_on_first_print() {
    let (vm, head, body) = test_vm(sample_request());
    vm.run(|lua| {
        lua.load(
            r#"
            server.sendStatus(404)
            server.sendHeader("Content-Type", "text/plain")
            server.sendCookie("sid", "s3cr3t", { path = "/", http_only = true })
            server.print("hello ", 42)
            "#,
        )
        .exec()
    })
    .expect("script");
    let (status, headers) = head.borrow().clone().expect("head committed");
    assert_eq!(status, 404);
    assert!(headers.contains(&("Content-Type".to_string(), "text/plain".to_string())));
    let set_cookie = headers
        .iter()
        .find(|(n, _)| n == "Set-Cookie")
        .map(|(_, v)| v.clone())
        .expect("Set-Cookie rendered");
    assert_eq!(set_cookie, "sid=s3cr3t; Path=/; Secure; HttpOnly");
    assert_eq!(body.borrow().as_slice(), b"hello 42");
}

#[test]
fn send_cookie_validates_options() {
    let (vm, _, _) = test_vm(sample_request());
    let (ok, msg) = call2(&vm, "return server.sendCookie('a', 'b', { nope = 1 })");
    assert!(!ok);
    assert_eq!(
        as_str(&msg),
        "'server.sendCookie(name, value[, options])': unknown option: nope"
    );
    // secure=false is a no-op (the flag only turns on) — the cookie still
    // renders Secure.
    let (ok, _) = call2(
        &vm,
        "return server.sendCookie('c', 'd', { secure = false })",
    );
    assert!(ok);
}

// ── json ─────────────────────────────────────────────────────────────────────

#[test]
fn json_round_trip_semantics() {
    let (vm, _, _) = test_vm(sample_request());

    let (ok, json) = call2(
        &vm,
        "return server.table_to_json({ a = 1, b = 'x', c = true })",
    );
    assert!(ok);
    let parsed: serde_json::Value = serde_json::from_str(&as_str(&json)).expect("valid json");
    assert_eq!(parsed["a"], 1);
    assert_eq!(parsed["b"], "x");
    assert_eq!(parsed["c"], true);
    // 3-space-indented rendering (the jansson JSON_INDENT(3) shape).
    assert!(as_str(&json).contains("\n   \""), "{json:?}");

    // Arrays keep iteration order; integral floats become JSON integers.
    let (ok, json) = call2(&vm, "return server.table_to_json({ 1, 2.0, 'three' })");
    assert!(ok);
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(&as_str(&json)).unwrap(),
        serde_json::json!([1, 2, "three"])
    );

    // Mixed keys are an error with the historical message.
    let (ok, msg) = call2(&vm, "return server.table_to_json({ 'a', x = 1 })");
    assert!(!ok);
    assert_eq!(
        as_str(&msg),
        "'server.table_to_json(table)': Cannot mix int and strings as key"
    );

    // The empty table rendered as (true, nil) on the C++ path.
    assert!(eval_bool(
        &vm,
        "local ok, v = server.table_to_json({}) return ok == true and v == nil",
    ));

    // json_to_table: objects by key, arrays 0-BASED (the historical jansson
    // walk), null values vanish.
    assert!(eval_bool(
        &vm,
        r#"
        local ok, t = server.json_to_table('{"a": [10, 20], "gone": null, "n": 1.5}')
        return ok and t.a[0] == 10 and t.a[1] == 20 and t.a[2] == nil
           and t.gone == nil and t.n == 1.5
        "#,
    ));

    let (ok, msg) = call2(&vm, "return server.json_to_table('{nope')");
    assert!(!ok);
    assert!(as_str(&msg).starts_with("'server.json_to_table(jsonstr)': Error parsing JSON:"));

    let (ok, msg) = call2(&vm, "return server.json_to_table('42')");
    assert!(!ok);
    assert_eq!(
        as_str(&msg),
        "'server.json_to_table(jsonstr)': Not a valid json string"
    );
}

// ── uuid / base62 ────────────────────────────────────────────────────────────

#[test]
fn uuid_bindings_and_golden_conversion() {
    let (vm, _, _) = test_vm(sample_request());
    assert!(eval_bool(
        &vm,
        "local ok, u = server.uuid() return ok and #u == 36 and u:sub(15,15) == '4'",
    ));
    assert!(eval_bool(
        &vm,
        "local ok, u = server.uuid62() return ok and u:find('-') ~= nil"
    ));
    // sole golden vector (computed with the C++ implementation).
    assert!(eval_bool(
        &vm,
        r#"
        local ok, b62 = server.uuid_to_base62('f81d4fae-7dec-11d0-a765-00a0c91e6bf6')
        if not (ok and b62 == 'LIhsBrTE21A-EN2J2swqbwM') then return false end
        local ok2, hex = server.base62_to_uuid(b62)
        return ok2 and hex == 'f81d4fae-7dec-11d0-a765-00a0c91e6bf6'
        "#,
    ));
    let (ok, _) = call2(&vm, "return server.base62_to_uuid('not/valid')");
    assert!(!ok);
    let (ok, _) = call2(&vm, "return server.uuid_to_base62('zzz')");
    assert!(!ok);
}

// ── jwt ──────────────────────────────────────────────────────────────────────

#[test]
fn jwt_round_trip_and_hardened_validation() {
    let (vm, _, _) = test_vm(sample_request());

    // Round-trip: generate (HS256) → decode (validated).
    assert!(eval_bool(
        &vm,
        r#"
        local ok, token = server.generate_jwt({ sub = 'u1', exp = server.systime() + 3600 })
        if not ok then return false end
        local ok2, claims = server.decode_jwt(token)
        return ok2 and claims.sub == 'u1'
        "#,
    ));

    // A token carrying an `aud` claim decodes: audience policy stays with
    // the script (jsonwebtoken's validate_aud default would reject it, which
    // would break every dsp-api token).
    assert!(eval_bool(
        &vm,
        r#"
        local ok, token = server.generate_jwt({
            sub = 'u1', aud = { 'Knora', 'Sipi' }, exp = server.systime() + 3600,
        })
        if not ok then return false end
        local ok2, claims = server.decode_jwt(token)
        -- JSON arrays land 0-based in Lua (json_to_table parity — pinned).
        return ok2 and claims.aud[0] == 'Knora' and claims.aud[1] == 'Sipi'
        "#,
    ));

    // Golden tokens (HS256, secret = the test config secret) — expired,
    // no-exp, wrong-alg — all rejected by the hardened validation.
    let secret = "UP 4888, nice 4-8-4 steam engine";
    let key = jsonwebtoken::EncodingKey::from_secret(secret.as_bytes());
    let expired = jsonwebtoken::encode(
        &jsonwebtoken::Header::default(),
        &serde_json::json!({"sub": "u1", "exp": 946684800}), // 2000-01-01
        &key,
    )
    .unwrap();
    let no_exp = jsonwebtoken::encode(
        &jsonwebtoken::Header::default(),
        &serde_json::json!({"sub": "u1"}),
        &key,
    )
    .unwrap();
    // alg: none with an empty signature — must be rejected by the pinned list.
    use base64::Engine as _;
    let b64 = |s: &str| base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(s);
    let alg_none = format!(
        "{}.{}.",
        b64(r#"{"alg":"none","typ":"JWT"}"#),
        b64(r#"{"sub":"u1","exp":33260871342}"#)
    );
    for (name, token) in [
        ("expired", expired),
        ("no-exp", no_exp),
        ("alg-none", alg_none),
    ] {
        let code = format!("return server.decode_jwt('{token}')");
        let (ok, msg) = call2(&vm, &code);
        assert!(!ok, "{name} must be rejected");
        assert!(
            as_str(&msg).starts_with("'server.decode_jwt(token)':"),
            "{name}"
        );
    }

    // A token signed with a different secret fails the signature check.
    let other = jsonwebtoken::encode(
        &jsonwebtoken::Header::default(),
        &serde_json::json!({"sub": "u1", "exp": 33260871342u64}),
        &jsonwebtoken::EncodingKey::from_secret(b"wrong secret"),
    )
    .unwrap();
    let (ok, _) = call2(&vm, &format!("return server.decode_jwt('{other}')"));
    assert!(!ok);
}

// ── requireAuth ──────────────────────────────────────────────────────────────

#[test]
fn require_auth_shapes() {
    use base64::Engine as _;
    let cases: Vec<(Option<String>, &str)> = vec![
        (None, "return t.status == 'NOAUTH'"),
        (
            Some(format!(
                "Basic {}",
                base64::engine::general_purpose::STANDARD.encode("user:pa:ss")
            )),
            "return t.status == 'BASIC' and t.username == 'user' and t.password == 'pa:ss'",
        ),
        (
            Some("Basic !!!notbase64!!!".to_string()),
            "return t.status == 'ERROR' and t.message == 'Auth-string not valid'",
        ),
        (
            Some("Bearer tok-42".to_string()),
            "return t.status == 'BEARER' and t.token == 'tok-42'",
        ),
        // Unknown space-separated scheme: the historical empty-table shape.
        (
            Some("Digest abc".to_string()),
            "return t.status == nil and next(t) == nil",
        ),
        (
            Some("Tokenwithoutspace".to_string()),
            "return t.status == 'ERROR' and t.message == 'Auth-type not known'",
        ),
    ];
    for (auth, probe) in cases {
        let mut req = sample_request();
        req.headers = match &auth {
            Some(v) => vec![("authorization".to_string(), v.clone())],
            None => vec![],
        };
        let (vm, _, _) = test_vm(req);
        let code =
            format!("local ok, t = server.requireAuth() if not ok then return false end {probe}");
        assert!(eval_bool(&vm, &code), "{auth:?}");
    }
}

// ── fs ───────────────────────────────────────────────────────────────────────

#[test]
fn fs_bindings_behave() {
    let dir = tempfile::tempdir().expect("tempdir");
    let base = dir.path().display().to_string();
    let (vm, _, _) = test_vm(sample_request());

    // mkdir with the decimal-mode interpretation (511 == 0o777).
    assert!(eval_bool(
        &vm,
        &format!("local ok = server.fs.mkdir('{base}/sub', 511) return ok"),
    ));
    assert!(eval_bool(
        &vm,
        &format!("local ok, t = server.fs.ftype('{base}/sub') return ok and t == 'DIRECTORY'"),
    ));
    std::fs::write(format!("{base}/sub/a.txt"), "hi").unwrap();
    std::fs::write(format!("{base}/sub/.hidden"), "x").unwrap();
    assert!(eval_bool(
        &vm,
        &format!(
            "local ok, names = server.fs.readdir('{base}/sub') \
             return ok and #names == 1 and names[1] == 'a.txt'"
        ),
    ));
    assert!(eval_bool(
        &vm,
        &format!("local ok, e = server.fs.exists('{base}/sub/a.txt') return ok and e == true"),
    ));
    assert!(eval_bool(
        &vm,
        &format!("local ok, m = server.fs.modtime('{base}/sub/a.txt') return ok and m > 0"),
    ));
    assert!(eval_bool(
        &vm,
        &format!("local ok = server.fs.copyFile('{base}/sub/a.txt', '{base}/sub/b.txt') return ok"),
    ));
    assert!(eval_bool(
        &vm,
        &format!("local ok = server.fs.moveFile('{base}/sub/b.txt', '{base}/sub/c.txt') return ok"),
    ));
    assert!(eval_bool(
        &vm,
        &format!("local ok = server.fs.unlink('{base}/sub/c.txt') return ok"),
    ));
    let (ok, msg) = call2(&vm, &format!("return server.fs.unlink('{base}/sub/c.txt')"));
    assert!(!ok);
    assert!(as_str(&msg).contains("File to unlink:"), "{msg:?}");
    assert!(eval_bool(
        &vm,
        "local ok, cwd = server.fs.getcwd() return ok and #cwd > 0"
    ));
    // The stat-based probe follows symlinks, so a missing path is an error.
    let (ok, _) = call2(&vm, &format!("return server.fs.ftype('{base}/absent')"));
    assert!(!ok);
}

#[test]
fn copy_tmpfile_uses_upload_index() {
    let dir = tempfile::tempdir().expect("tempdir");
    let src = dir.path().join("upload.bin");
    std::fs::write(&src, b"payload").unwrap();
    let mut req = sample_request();
    req.uploads[0].tmpname = src.display().to_string();
    let (vm, _, _) = test_vm(req);
    let target = dir.path().join("copied.bin").display().to_string();
    assert!(eval_bool(
        &vm,
        &format!("local ok = server.copyTmpfile(1, '{target}') return ok"),
    ));
    assert_eq!(std::fs::read(&target).unwrap(), b"payload");
    let (ok, msg) = call2(&vm, &format!("return server.copyTmpfile(9, '{target}')"));
    assert!(!ok);
    assert_eq!(
        as_str(&msg),
        "'lua_copytmpfile(from,to)': parameter 'from' is not a valid index."
    );
}

// ── mimetype / log / systime ─────────────────────────────────────────────────

#[test]
fn parse_mimetype_shapes() {
    let (vm, _, _) = test_vm(sample_request());
    assert!(eval_bool(
        &vm,
        r#"
        local ok, m = server.parse_mimetype('Text/HTML; charset="UTF-8"')
        return ok and m.mimetype == 'text/html' and m.charset == 'utf-8'
        "#,
    ));
    assert!(eval_bool(
        &vm,
        "local ok, m = server.parse_mimetype('image/jp2') return ok and m.mimetype == 'image/jp2' and m.charset == nil",
    ));
    let (ok, msg) = call2(
        &vm,
        "return server.parse_mimetype('image/tiff; boundary=x')",
    );
    assert!(!ok);
    assert!(
        as_str(&msg).contains("Could not parse MIME type"),
        "{msg:?}"
    );
}

#[test]
fn log_and_systime() {
    let (vm, _, _) = test_vm(sample_request());
    assert!(eval_bool(
        &vm,
        "local ok = server.log('hello', server.loglevel.LOG_DEBUG) return ok"
    ));
    let (ok, msg) = call2(&vm, "return server.log('x', 'notanint')");
    assert!(!ok);
    assert_eq!(as_str(&msg), "'server.log()': level is not integer");
    assert!(eval_bool(
        &vm,
        "return type(server.systime()) == 'number' and server.systime() > 1600000000"
    ));
}

// ── http (local mock; redirects + shape) ─────────────────────────────────────

fn spawn_mock(response: &'static str) -> String {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("bind");
    let addr = listener.local_addr().unwrap();
    std::thread::spawn(move || {
        if let Ok((mut sock, _)) = listener.accept() {
            use std::io::{Read as _, Write as _};
            let mut buf = [0u8; 4096];
            let _ = sock.read(&mut buf);
            let _ = sock.write_all(response.as_bytes());
        }
    });
    format!("http://{addr}")
}

#[test]
fn http_get_result_table() {
    let url =
        spawn_mock("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello");
    let (vm, _, _) = test_vm(sample_request());
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, r = server.http('GET', '{url}/x', 2000)
            return ok and r.status_code == 200 and r.body == 'hello'
               and r.header['content-type'] == 'text/plain' and r.duration >= 0
            "#
        ),
    ));
}

#[test]
fn http_does_not_follow_redirects() {
    let url = spawn_mock(
        "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/void\r\nContent-Length: 0\r\n\r\n",
    );
    let (vm, _, _) = test_vm(sample_request());
    assert!(eval_bool(
        &vm,
        &format!(
            "local ok, r = server.http('GET', '{url}/r', 2000) return ok and r.status_code == 302"
        ),
    ));
}

#[test]
fn http_total_timeout_fails_cleanly() {
    // A mock that accepts and never answers: the total-request timeout (not
    // just connect) must fail the call within the script's budget.
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("bind");
    let addr = listener.local_addr().unwrap();
    std::thread::spawn(move || {
        let _held = listener.accept();
        std::thread::sleep(std::time::Duration::from_secs(5));
    });
    let (vm, _, _) = test_vm(sample_request());
    let started = std::time::Instant::now();
    let (ok, msg) = call2(
        &vm,
        &format!("return server.http('GET', 'http://{addr}/hang', 300)"),
    );
    assert!(!ok);
    assert!(as_str(&msg).contains("failed"), "{msg:?}");
    assert!(started.elapsed() < std::time::Duration::from_secs(3));
}

#[test]
fn http_rejects_non_get() {
    let (vm, _, _) = test_vm(sample_request());
    let (ok, msg) = call2(&vm, "return server.http('POST', 'http://127.0.0.1:1/')");
    assert!(!ok);
    assert_eq!(
        as_str(&msg),
        "'server.http(method, url, [header])': unknown method POST"
    );
}

// ── sqlite ───────────────────────────────────────────────────────────────────

#[test]
fn sqlite_round_trip() {
    let dir = tempfile::tempdir().expect("tempdir");
    let db_path = dir.path().join("test.db").display().to_string();
    let (vm, _, _) = test_vm(sample_request());
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local db = sqlite('{db_path}', 'CRW')
            local qry = db << 'CREATE TABLE t (id INTEGER, name TEXT, score REAL)'
            qry()
            qry = ~qry
            qry = db << 'INSERT INTO t VALUES (?, ?, ?)'
            qry(1, 'one', 1.5)
            qry(2, 'two', 2.5)
            qry = ~qry
            qry = db << 'SELECT id, name, score FROM t ORDER BY id'
            local row = qry()
            -- row tables keep the historical 0-based column keys
            if not (row[0] == 1 and row[1] == 'one' and row[2] == 1.5) then return false end
            row = qry()
            if not (row[0] == 2 and row[1] == 'two') then return false end
            row = qry()
            if row ~= nil then return false end
            if not tostring(db):find('DB%-File:') then return false end
            qry = ~qry
            db = ~db
            return true
            "#
        ),
    ));
}

#[test]
fn sqlite_error_convention_is_lua_errors() {
    let dir = tempfile::tempdir().expect("tempdir");
    let db_path = dir.path().join("err.db").display().to_string();
    let (vm, _, _) = test_vm(sample_request());
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local db = sqlite('{db_path}', 'CRW')
            -- syntax errors raise (pcall-trapped), never (false, msg)
            local trapped = pcall(function() return db << 'NOT REAL SQL' end)
            if trapped then return false end
            -- a statement outliving ~db raises a clean error, not a crash
            local qry = db << 'CREATE TABLE t (id INTEGER)'
            db = ~db
            local trapped2, err = pcall(function() return qry() end)
            return trapped2 == false and tostring(err):find('database is closed', 1, true) ~= nil
            "#
        ),
    ));
    // Opening a nonexistent path read-only raises.
    assert!(eval_bool(
        &vm,
        "local ok = pcall(function() return sqlite('/no/such/dir/x.db', 'RO') end) return ok == false",
    ));
}

// ── chokepoint enumeration ───────────────────────────────────────────────────

#[test]
fn every_binding_registers_through_the_chokepoint() {
    let (vm, _, _) = test_vm(sample_request());
    vm.verify_bindings_checked(bindings::BINDING_TABLES)
        .expect("all bindings chokepointed");
}
