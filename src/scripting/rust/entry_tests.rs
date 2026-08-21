//! `LuaEnv` entry-point tests: the boot probes (fail-closed init), and the
//! preflight return-shape matrix — bare permission string, permission table,
//! deny, direct response (`false` after `sendStatus`), legacy `restrict:…`
//! strings (invalid under the current contract), and the malformed shapes.

use std::io::Write;
use std::path::PathBuf;
use std::time::Duration;

use scripting::bindings::{ConfigValues, RequestData};
use scripting::{HookProbes, KillReason, LimitConfig, LuaEnv, PreflightFailure, PreflightReply};

fn write_init(dir: &std::path::Path, body: &str) -> PathBuf {
    let path = dir.join("init.lua");
    let mut f = std::fs::File::create(&path).expect("create init");
    f.write_all(body.as_bytes()).expect("write init");
    path
}

fn env_with(dir: &std::path::Path, init_body: &str) -> LuaEnv {
    env_with_limits(dir, init_body, LimitConfig::default())
}

fn env_with_limits(dir: &std::path::Path, init_body: &str, limits: LimitConfig) -> LuaEnv {
    let init = write_init(dir, init_body);
    LuaEnv::new(
        dir.to_path_buf(),
        Some(init),
        "test-secret".into(),
        limits,
        ConfigValues::default(),
    )
}

fn req_with_cookie() -> RequestData {
    RequestData {
        headers: vec![("cookie".into(), "sid=abc".into())],
        ..Default::default()
    }
}

#[test]
fn probes_report_defined_hooks() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        "function pre_flight(prefix, identifier, cookie) return 'deny' end",
    );
    let HookProbes {
        pre_flight,
        file_pre_flight,
    } = env.probe_hooks().expect("probe");
    assert!(pre_flight);
    assert!(!file_pre_flight);
}

#[test]
fn probe_fails_closed_on_init_error() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(dir.path(), "error('boom in init')");
    let err = env.probe_hooks().expect_err("init error must be fatal");
    assert!(err.to_string().contains("boom in init"), "{err}");
}

#[test]
fn probe_fails_closed_on_init_syntax_error() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(dir.path(), "function broken(");
    assert!(env.probe_hooks().is_err());
}

#[test]
fn no_init_script_means_no_hooks() {
    let dir = tempfile::tempdir().unwrap();
    let env = LuaEnv::new(
        dir.path().to_path_buf(),
        None,
        String::new(),
        LimitConfig::default(),
        ConfigValues::default(),
    );
    let probes = env.probe_hooks().expect("probe without init");
    assert!(!probes.pre_flight);
    assert!(!probes.file_pre_flight);
}

#[test]
fn shape_allow_with_path() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        r#"
        function pre_flight(prefix, identifier, cookie)
            seen_args = prefix .. '/' .. identifier .. '/' .. cookie
            return 'allow', '/data/' .. prefix .. '/' .. identifier
        end
        "#,
    );
    match env.preflight(req_with_cookie(), "unit", "lena.jp2") {
        Ok(PreflightReply::Decision { permission, kv }) => {
            assert_eq!(permission, "allow");
            assert_eq!(
                kv,
                vec![("infile".to_string(), "/data/unit/lena.jp2".to_string())]
            );
        }
        other => panic!(
            "expected allow decision, got {other:?}",
            other = fmt(&other)
        ),
    }
}

#[test]
fn shape_deny_needs_no_path() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(dir.path(), "function pre_flight() return 'deny' end");
    match env.preflight(RequestData::default(), "p", "i") {
        Ok(PreflightReply::Decision { permission, kv }) => {
            assert_eq!(permission, "deny");
            assert_eq!(kv, vec![("infile".to_string(), String::new())]);
        }
        other => panic!("expected deny, got {other:?}", other = fmt(&other)),
    }
}

#[test]
fn shape_restrict_table_with_options() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        r#"
        function pre_flight(prefix, identifier, cookie)
            return { type = 'restrict', watermark = '/wm.tif', size = '!256,256' }, '/data/img.jp2'
        end
        "#,
    );
    match env.preflight(RequestData::default(), "p", "i") {
        Ok(PreflightReply::Decision { permission, kv }) => {
            assert_eq!(permission, "restrict");
            let get = |k: &str| kv.iter().find(|(key, _)| key == k).map(|(_, v)| v.as_str());
            assert_eq!(get("watermark"), Some("/wm.tif"));
            assert_eq!(get("size"), Some("!256,256"));
            assert_eq!(get("infile"), Some("/data/img.jp2"));
        }
        other => panic!(
            "expected restrict decision, got {other:?}",
            other = fmt(&other)
        ),
    }
}

#[test]
fn shape_direct_response_false_after_send_status() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        r#"
        function pre_flight(prefix, identifier, cookie)
            server.sendStatus(401)
            server.sendHeader('WWW-Authenticate', 'Bearer')
            server.print('denied')
            return false
        end
        "#,
    );
    match env.preflight(RequestData::default(), "p", "i") {
        Ok(PreflightReply::Direct {
            status,
            headers,
            body,
        }) => {
            assert_eq!(status, 401);
            assert!(headers.contains(&("WWW-Authenticate".to_string(), "Bearer".to_string())));
            assert_eq!(body, b"denied");
        }
        other => panic!(
            "expected direct response, got {other:?}",
            other = fmt(&other)
        ),
    }
}

#[test]
fn direct_response_wins_even_when_the_hook_then_errors() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        r#"
        function pre_flight()
            server.sendStatus(500)
            error('after answering')
        end
        "#,
    );
    match env.preflight(RequestData::default(), "p", "i") {
        Ok(PreflightReply::Direct { status, .. }) => assert_eq!(status, 500),
        other => panic!(
            "expected direct response, got {other:?}",
            other = fmt(&other)
        ),
    }
}

#[test]
fn shape_legacy_restrict_string_is_invalid() {
    // The colon-option form predates the seam; the current contract rejects
    // it as an unknown permission (pinned).
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        "function pre_flight() return 'restrict:size=!256,256', '/f' end",
    );
    match env.preflight(RequestData::default(), "p", "i") {
        Err(PreflightFailure::Error(msg)) => {
            assert!(msg.contains("is not valid: restrict:size"), "{msg}");
        }
        other => panic!(
            "expected invalid-permission error, got {other:?}",
            other = fmt(&other)
        ),
    }
}

#[test]
fn malformed_shapes_fail() {
    let cases = [
        (
            "function pre_flight() end",
            "must return at least one value",
        ),
        ("function pre_flight() return 42 end", "was not valid"),
        ("function pre_flight() return {} end", "has no type field"),
        (
            "function pre_flight() return { type = 7 } end",
            "must be a string",
        ),
        (
            "function pre_flight() return { type = 'allow', watermark = {} } end",
            "must be a string",
        ),
        (
            "function pre_flight() return 'allow' end",
            "did not return a file path",
        ),
        (
            "function pre_flight() return 'allow', 42 end",
            "was not a string",
        ),
    ];
    for (init, needle) in cases {
        let dir = tempfile::tempdir().unwrap();
        let env = env_with(dir.path(), init);
        match env.preflight(RequestData::default(), "p", "i") {
            Err(PreflightFailure::Error(msg)) => {
                assert!(msg.contains(needle), "{init}: {msg}");
            }
            other => panic!("{init}: expected error, got {other:?}", other = fmt(&other)),
        }
    }
}

#[test]
fn file_preflight_rejects_extended_permissions() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        r#"
        function file_pre_flight(filepath, cookie)
            return 'clickthrough', filepath
        end
        "#,
    );
    match env.file_preflight(RequestData::default(), "/data/x.pdf") {
        Err(PreflightFailure::Error(msg)) => {
            assert!(msg.contains("is not valid: clickthrough"), "{msg}");
        }
        other => panic!("expected error, got {other:?}", other = fmt(&other)),
    }
}

#[test]
fn file_preflight_passes_filepath_and_cookie() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(
        dir.path(),
        r#"
        function file_pre_flight(filepath, cookie)
            if cookie ~= 'sid=abc' then return 'deny' end
            return 'allow', filepath
        end
        "#,
    );
    match env.file_preflight(req_with_cookie(), "/data/doc.pdf") {
        Ok(PreflightReply::Decision { permission, kv }) => {
            assert_eq!(permission, "allow");
            assert_eq!(kv[0].1, "/data/doc.pdf");
        }
        other => panic!("expected allow, got {other:?}", other = fmt(&other)),
    }
}

#[test]
fn hook_missing_at_request_time_fails_closed() {
    // The boot probe froze has_preflight=true; if the hook vanished (script
    // edited post-boot), the request fails closed with a 500-class error.
    let dir = tempfile::tempdir().unwrap();
    let env = env_with(dir.path(), "-- no hooks defined");
    match env.preflight(RequestData::default(), "p", "i") {
        Err(PreflightFailure::Error(_)) => {}
        other => panic!(
            "expected fail-closed error, got {other:?}",
            other = fmt(&other)
        ),
    }
}

#[test]
fn duration_recorder_observes_vm_build_and_script() {
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::sync::Arc;

    static VM_BUILDS: AtomicU64 = AtomicU64::new(0);
    static SCRIPTS: AtomicU64 = AtomicU64::new(0);
    let entries: Arc<std::sync::Mutex<Vec<&'static str>>> = Arc::default();
    let seen = Arc::clone(&entries);
    scripting::set_duration_recorder(scripting::DurationRecorder {
        vm_build: Box::new(|_, secs| {
            assert!(secs >= 0.0);
            VM_BUILDS.fetch_add(1, Ordering::Relaxed);
        }),
        script: Box::new(move |entry, secs| {
            assert!(secs >= 0.0);
            seen.lock().unwrap().push(entry);
            SCRIPTS.fetch_add(1, Ordering::Relaxed);
        }),
    });

    let dir = tempfile::tempdir().unwrap();
    let env = env_with(dir.path(), "function pre_flight() return 'deny' end");
    let builds_before = VM_BUILDS.load(Ordering::Relaxed);
    let scripts_before = SCRIPTS.load(Ordering::Relaxed);
    let _ = env.preflight(RequestData::default(), "p", "i");

    assert!(
        VM_BUILDS.load(Ordering::Relaxed) > builds_before,
        "a preflight must record a VM build"
    );
    assert!(
        SCRIPTS.load(Ordering::Relaxed) > scripts_before,
        "a preflight must record a script run"
    );
    assert!(
        entries.lock().unwrap().contains(&"pre_flight"),
        "the script sample carries the entry-point label"
    );
}

#[test]
fn killed_preflight_reports_the_kill() {
    let dir = tempfile::tempdir().unwrap();
    let env = env_with_limits(
        dir.path(),
        "function pre_flight() while true do end end",
        LimitConfig {
            timeout: Duration::from_millis(50),
            ..LimitConfig::default()
        },
    );
    match env.preflight(RequestData::default(), "p", "i") {
        Err(PreflightFailure::Killed(KillReason::Timeout)) => {}
        other => panic!("expected timeout kill, got {other:?}", other = fmt(&other)),
    }
}

fn fmt(r: &Result<PreflightReply, PreflightFailure>) -> String {
    match r {
        Ok(PreflightReply::Decision { permission, kv }) => {
            format!("Decision({permission}, {kv:?})")
        }
        Ok(PreflightReply::Direct { status, .. }) => format!("Direct({status})"),
        Err(e) => format!("Err({e:?})"),
    }
}
