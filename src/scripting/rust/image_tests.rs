//! `SipiImage` binding tests over the real engine (`sipi_image_*` handle
//! ABI): create/dims/crop/scale/rotate/topleft, write (Essentials stamping
//! path shape), send-to-sink streaming, error shapes, upload-index sourcing,
//! `helper.filename_hash`, and the file-mimetype seam helpers — the seam
//! probes for the new ABI, driven from Lua exactly as scripts drive it.

use std::cell::RefCell;
use std::path::PathBuf;
use std::rc::Rc;

use mlua::{MultiValue, Value};
use runfiles::{rlocation, Runfiles};
use scripting::bindings::{self, BindingCtx, ConfigValues, RequestData, ResponseWriter, Upload};
use scripting::{LimitConfig, RequestVm, ScriptRuntime};

fn fixture(rel: &str) -> PathBuf {
    let r = Runfiles::create().expect("runfiles");
    let path = rlocation!(r, format!("_main/test/_test_data/images/{rel}")).expect("fixture");
    // Resolve the runfiles symlink: libmagic sniffs the link itself
    // ("inode/symlink") rather than the target.
    std::fs::canonicalize(path).expect("canonical fixture path")
}

type Body = Rc<RefCell<Vec<u8>>>;

fn test_vm(req: RequestData) -> (RequestVm, Body) {
    let rt = ScriptRuntime::new(std::env::temp_dir(), LimitConfig::default());
    let vm = rt.request_vm().expect("vm");
    let body: Body = Rc::default();
    let write_body = Rc::clone(&body);
    let writer = ResponseWriter::new(
        Box::new(|_, _| {}),
        Box::new(move |data| {
            write_body.borrow_mut().extend_from_slice(data);
            Ok(())
        }),
    );
    let ctx = BindingCtx {
        request: Rc::new(req),
        response: Rc::new(RefCell::new(writer)),
        config: Rc::new(ConfigValues::default()),
    };
    bindings::install(&vm, &ctx).expect("install bindings");
    (vm, body)
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

#[test]
fn new_dims_and_transforms() {
    let (vm, _) = test_vm(RequestData::default());
    let path = fixture("unit/lena512.tif").display().to_string();
    let ok = eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{path}')
            if not ok then return false end
            local ok2, d = img:dims()
            if not (ok2 and d.nx == 512 and d.ny == 512) then return false end
            -- path form: header-only shape probe
            local ok3, d2 = SipiImage.dims('{path}')
            if not (ok3 and d2.nx == 512 and d2.ny == 512) then return false end
            -- crop to a IIIF region, then check the shrink
            local ok4 = img:crop('0,0,100,80')
            local _, d3 = img:dims()
            if not (ok4 and d3.nx == 100 and d3.ny == 80) then return false end
            local ok5 = img:scale('50,')
            local _, d4 = img:dims()
            if not (ok5 and d4.nx == 50) then return false end
            local ok6 = img:rotate(90.0)
            if not ok6 then return false end
            -- topleft returns a single bare true (the historical shape)
            local r = img:topleft()
            if r ~= true then return false end
            -- tostring goes through the userdata metatable
            if not tostring(img):find('File:', 1, true) then return false end
            return true
            "#
        ),
    );
    assert!(ok);
}

#[test]
fn new_error_shapes() {
    let (vm, _) = test_vm(RequestData::default());
    let (ok, msg) = call2(&vm, "return SipiImage.new('/does/not/exist.tif')");
    assert!(!ok);
    let text = match &msg {
        Value::String(s) => s.to_string_lossy(),
        other => panic!("expected message, got {other:?}"),
    };
    assert!(text.starts_with("SipiImage.new():"), "{text}");

    let (ok, _) = call2(&vm, "return SipiImage.new(3)");
    assert!(!ok, "invalid upload index");

    let (ok, msg) = call2(&vm, "return SipiImage.new('x.tif', { nope = 'y' })");
    assert!(!ok);
    assert!(matches!(&msg, Value::String(s)
        if s.to_string_lossy() == "SipiImage.new(): Error in parameter table (unknown parameter)"));
}

#[test]
fn new_from_upload_index() {
    let src = fixture("unit/lena512.tif").display().to_string();
    let req = RequestData {
        uploads: vec![Upload {
            fieldname: "file".into(),
            origname: "lena512.tif".into(),
            tmpname: src,
            mimetype: "image/tiff".into(),
            filesize: 266_786,
        }],
        ..Default::default()
    };
    let (vm, _) = test_vm(req);
    assert!(eval_bool(
        &vm,
        r#"
        local ok, img = SipiImage.new(1)
        if not ok then return false end
        local ok2, d = img:dims()
        return ok2 and d.nx == 512
        "#,
    ));
    // mimetype_consistency compares the recorded source path's content.
    assert!(eval_bool(
        &vm,
        r#"
        local ok, img = SipiImage.new(1)
        local ok2, consistent = img:mimetype_consistency('image/tiff', 'lena512.tif')
        return ok and ok2 and consistent == true
        "#,
    ));
    assert!(eval_bool(
        &vm,
        r#"
        local ok, img = SipiImage.new(1)
        local ok2, consistent = img:mimetype_consistency('image/png', 'lena512.png')
        return ok and ok2 and consistent == false
        "#,
    ));
}

#[test]
fn write_and_service_file_stamping() {
    let dir = tempfile::tempdir().expect("tempdir");
    let out = dir.path().join("out.jp2").display().to_string();
    let src = fixture("unit/lena512.tif").display().to_string();
    let (vm, _) = test_vm(RequestData::default());

    // Plain write.
    let plain_out = dir.path().join("plain.png").display().to_string();
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            local ok2, written = img:write('{plain_out}')
            return ok and ok2 and written == '{plain_out}'
            "#
        ),
    ));
    assert!(std::fs::metadata(&plain_out).unwrap().len() > 0);

    // Service-file write (Essentials packet stamped engine-side).
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            local ok2, written = img:write('{out}', {{
                file_role = 'service-file',
                origname = 'lena512.tif',
                mimetype = 'image/tiff',
            }})
            return ok and ok2 and written == '{out}'
            "#
        ),
    ));
    assert!(std::fs::metadata(&out).unwrap().len() > 0);

    // The lua_error convention: invalid enum values raise (pcall-trapped).
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            if not ok then return false end
            local trapped, err = pcall(function()
                return img:write('{out}', {{ Sprofile = 'BOGUS' }})
            end)
            return trapped == false and tostring(err):find('invalid Sprofile!', 1, true) ~= nil
            "#
        ),
    ));
    // service-file with a .png output raises.
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            local trapped = pcall(function()
                return img:write('{plain_out}', {{ file_role = 'service-file', origname = 'x', mimetype = 'y' }})
            end)
            return trapped == false
            "#
        ),
    ));
}

#[test]
fn send_streams_to_the_response_sink() {
    let src = fixture("unit/lena512.tif").display().to_string();
    let (vm, body) = test_vm(RequestData::default());
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            if not ok then return false end
            local ok2 = img:send('jpg')
            return ok2 == true
            "#
        ),
    ));
    let bytes = body.borrow();
    assert!(bytes.len() > 1000, "streamed {} bytes", bytes.len());
    assert_eq!(&bytes[..2], &[0xFF, 0xD8], "JPEG SOI marker");
    drop(bytes);

    // jp2 is accepted by write()'s extension table but NOT by send()'s
    // (the historical asymmetry, pinned).
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            local trapped = pcall(function() return img:send('jp2') end)
            return trapped == false
            "#
        ),
    ));
}

#[test]
fn filename_hash_and_file_mimetype_helpers() {
    let src = fixture("unit/lena512.tif").display().to_string();
    let (vm, _) = test_vm(RequestData::default());
    assert!(eval_bool(
        &vm,
        r#"
        local ok, path = helper.filename_hash('lena512.jp2')
        return ok and path:find('lena512.jp2', 1, true) ~= nil
        "#,
    ));
    let (ok, msg) = call2(&vm, "return helper.filename_hash()");
    assert!(!ok);
    assert!(matches!(&msg, Value::String(s)
        if s.to_string_lossy() == "'helper.hash(filename)': parameter missing"));

    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, m = server.file_mimetype('{src}')
            return ok and m.mimetype == 'image/tiff'
            "#
        ),
    ));
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, consistent = server.file_mimeconsistency('{src}')
            return ok and type(consistent) == 'boolean'
            "#
        ),
    ));
    let (ok, _) = call2(&vm, "return server.file_mimetype(7)");
    assert!(!ok, "invalid upload index form");
}

#[test]
fn exif_reads_from_a_tagged_fixture() {
    // exif_lens_specification.tif carries EXIF metadata.
    let src = fixture("unit/exif_lens_specification.tif")
        .display()
        .to_string();
    let (vm, _) = test_vm(RequestData::default());
    // An unrecognized tag is the (false, msg) shape — never UB (the
    // historical end()-deref is fixed in the reimplementation).
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            if not ok then return false end
            local ok2, msg = img:exif('NotATag')
            return ok2 == false and msg == 'SipiImage.exif(): Unrecognized EXIF-Tag'
            "#
        ),
    ));
    // A recognized-but-absent or present tag: either (true, value) or the
    // "not available" message — both are valid shapes for this fixture.
    assert!(eval_bool(
        &vm,
        &format!(
            r#"
            local ok, img = SipiImage.new('{src}')
            local ok2, v = img:exif('Make')
            if ok2 then return type(v) == 'string' end
            return v == 'SipiImage.exif(): requested exif tag not available'
                or v == 'SipiImage.exif(): no exif data available'
            "#
        ),
    ));
}
