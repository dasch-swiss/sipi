//! The `SipiImage` binding: a global table of chokepointed functions (the
//! historical shape — `img:method(...)` resolves through the userdata
//! metatable's `__index`, which IS the `SipiImage` table, so the enumeration
//! check covers every method) over the engine's `sipi_image_*` handle ABI.
//! The userdata owns the handle; a killed/unwound VM frees it in `Drop`.
//!
//! Error conventions are shape-preserved from the C++ bindings: argument and
//! engine failures return `(false, msg)`; invalid compression values and
//! unsupported output extensions raise (the historical `lua_error`
//! convention — under mlua the raised message renders with a
//! `runtime error: ` prefix, a documented divergence).

use std::cell::RefCell;
use std::rc::Rc;

use mlua::{AnyUserData, Lua, MultiValue, Table, Value, Variadic};

use crate::engine_ffi::{self, ExifValue, GpsValue, ImageHandle};
use crate::runtime::RequestVm;

use super::{BindingCtx, RequestData, ResponseWriter, Upload};

/// The `SipiImage` userdata payload.
pub struct LuaImage {
    handle: RefCell<ImageHandle>,
}

fn fail(lua: &Lua, msg: impl AsRef<str>) -> mlua::Result<MultiValue> {
    Ok(MultiValue::from_iter([
        Value::Boolean(false),
        Value::String(lua.create_string(msg.as_ref())?),
    ]))
}

fn ok2(value: Value) -> mlua::Result<MultiValue> {
    Ok(MultiValue::from_iter([Value::Boolean(true), value]))
}

fn ok_nil() -> mlua::Result<MultiValue> {
    Ok(MultiValue::from_iter([Value::Boolean(true), Value::Nil]))
}

fn coerce_string(v: &Value) -> Option<String> {
    match v {
        Value::String(s) => Some(s.to_string_lossy()),
        Value::Integer(i) => Some(i.to_string()),
        Value::Number(n) => Some(n.to_string()),
        _ => None,
    }
}

fn image_arg<'a>(
    lua: &Lua,
    args: &'a Variadic<Value>,
    who: &str,
) -> Result<&'a AnyUserData, mlua::Result<MultiValue>> {
    match args.first() {
        Some(Value::UserData(ud)) if ud.is::<LuaImage>() => Ok(ud),
        _ => Err(fail(lua, format!("SipiImage.{who}(): not a valid image"))),
    }
}

/// Installs the `SipiImage` global table + registers the userdata type whose
/// `__index`/`__metatable` point at it.
pub fn install(vm: &RequestVm, ctx: &BindingCtx) -> mlua::Result<()> {
    let lua = vm.lua();
    let table = lua.create_table()?;
    let req = Rc::clone(&ctx.request);
    let resp = Rc::clone(&ctx.response);

    {
        let req = Rc::clone(&req);
        vm.register_binding("SipiImage", &table, "new", move |lua, args| {
            image_new(lua, &req, args)
        })?;
    }
    vm.register_binding("SipiImage", &table, "dims", image_dims)?;
    vm.register_binding("SipiImage", &table, "exif", image_exif)?;
    vm.register_binding("SipiImage", &table, "gps", image_gps)?;
    vm.register_binding("SipiImage", &table, "crop", image_crop)?;
    vm.register_binding("SipiImage", &table, "scale", image_scale)?;
    vm.register_binding("SipiImage", &table, "rotate", image_rotate)?;
    vm.register_binding("SipiImage", &table, "topleft", image_topleft)?;
    vm.register_binding("SipiImage", &table, "watermark", image_watermark)?;
    vm.register_binding(
        "SipiImage",
        &table,
        "mimetype_consistency",
        image_mimetype_consistency,
    )?;
    {
        let resp = Rc::clone(&resp);
        vm.register_binding("SipiImage", &table, "write", move |lua, args| {
            image_write(lua, &resp, args)
        })?;
    }
    {
        let resp = Rc::clone(&resp);
        vm.register_binding("SipiImage", &table, "send", move |lua, args| {
            image_send(lua, &resp, args)
        })?;
    }

    // The userdata metatable's __index IS the methods table, so
    // `img:method(...)` dispatches through the chokepointed functions. (The
    // historical `__metatable` obfuscation — getmetatable(img) returning the
    // methods table — is not replicated: mlua restricts that metamethod, and
    // no script reads it.)
    let index_table = table.clone();
    lua.register_userdata_type::<LuaImage>(move |reg| {
        use mlua::{UserDataFields, UserDataMethods};
        reg.add_meta_field("__index", index_table);
        reg.add_meta_method("__tostring", |_, this: &LuaImage, (): ()| {
            Ok(this.handle.borrow().to_display_string())
        });
    })?;

    lua.globals().set("SipiImage", table)
}

fn image_new(lua: &Lua, req: &RequestData, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.is_empty() {
        return fail(lua, "SipiImage.new(): No filename given");
    }
    let (imgpath, mut original): (String, Option<String>) = match args.first() {
        Some(Value::Integer(index)) => {
            let Some(upload) = usize::try_from(index - 1)
                .ok()
                .and_then(|i: usize| req.uploads.get(i))
            else {
                return fail(
                    lua,
                    "'SipiImage.new()': Could not read data of uploaded file. Invalid index?",
                );
            };
            let Upload {
                tmpname, origname, ..
            } = upload;
            (tmpname.clone(), Some(origname.clone()))
        }
        Some(Value::String(s)) => (s.to_string_lossy(), None),
        _ => return fail(lua, "SipiImage.new(): filename must be string or index"),
    };

    let mut region: Option<String> = None;
    let mut size: Option<String> = None;
    let mut reduce: Option<i64> = None;
    if let Some(second) = args.get(1) {
        let Value::Table(opts) = second else {
            return fail(lua, "SipiImage.new(): Second parameter must be table");
        };
        for pair in opts.pairs::<Value, Value>() {
            let (key, value) = pair?;
            let Some(param) = coerce_string(&key) else {
                continue; // non-string option keys are skipped, as historically
            };
            match param.as_str() {
                "region" => match &value {
                    Value::String(s) => region = Some(s.to_string_lossy()),
                    _ => return fail(lua, "SipiImage.new(): Error in region parameter"),
                },
                "size" => match &value {
                    Value::String(s) => size = Some(s.to_string_lossy()),
                    _ => return fail(lua, "SipiImage.new(): Error in size parameter"),
                },
                "reduce" => match &value {
                    Value::Integer(i) => reduce = Some(*i),
                    Value::Number(n) => reduce = Some(*n as i64),
                    _ => return fail(lua, "SipiImage.new(): Error in reduce parameter"),
                },
                "original" => match &value {
                    Value::String(s) => original = Some(s.to_string_lossy()),
                    _ => return fail(lua, "SipiImage.new(): Error in original parameter"),
                },
                // Parsed but no longer used — the hash type is decided at
                // write time (ADR-0010); retained on the Lua-facing API.
                "hash" => match &value {
                    Value::String(s) => {
                        let h = s.to_string_lossy();
                        if !matches!(h.as_str(), "md5" | "sha1" | "sha256" | "sha384" | "sha512") {
                            return fail(lua, "SipiImage.new(): Error in hash type");
                        }
                    }
                    _ => return fail(lua, "SipiImage.new(): Error in hash parameter"),
                },
                _ => {
                    return fail(
                        lua,
                        "SipiImage.new(): Error in parameter table (unknown parameter)",
                    );
                }
            }
        }
    }

    match ImageHandle::new(
        &imgpath,
        region.as_deref(),
        size.as_deref(),
        reduce,
        original.as_deref(),
    ) {
        Err(msg) => fail(lua, format!("SipiImage.new(): {msg}")),
        Ok(handle) => {
            let ud = lua.create_any_userdata(LuaImage {
                handle: RefCell::new(handle),
            })?;
            ok2(Value::UserData(ud))
        }
    }
}

fn dims_table(lua: &Lua, nx: u64, ny: u64, orientation: i32) -> mlua::Result<MultiValue> {
    let out = lua.create_table()?;
    out.set("nx", nx)?;
    out.set("ny", ny)?;
    out.set("orientation", orientation)?;
    ok2(Value::Table(out))
}

fn image_dims(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() != 1 {
        return fail(lua, "SipiImage.dims(): Incorrect number of arguments");
    }
    match &args[0] {
        Value::String(path) => match engine_ffi::file_dims(&path.to_string_lossy()) {
            Err(msg) if msg == "Couldn't get dimensions" => {
                fail(lua, "SipiImage.dims(): Couldn't get dimensions")
            }
            Err(msg) => fail(lua, format!("SipiImage.dims(): {msg}")),
            Ok((nx, ny, orientation)) => dims_table(lua, nx, ny, orientation),
        },
        Value::UserData(ud) if ud.is::<LuaImage>() => {
            let img = ud.borrow::<LuaImage>()?;
            let outcome = img.handle.borrow().dims();
            match outcome {
                Err(msg) => fail(lua, format!("SipiImage.dims(): {msg}")),
                Ok((nx, ny, orientation)) => dims_table(lua, nx, ny, orientation),
            }
        }
        _ => fail(lua, "SipiImage.dims(): not a valid image"),
    }
}

fn image_exif(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() != 2 {
        return fail(lua, "SipiImage.exif(): Incorrect number of arguments");
    }
    let ud = match image_arg(lua, &args, "exif") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let Some(tag) = coerce_string(&args[1]) else {
        return fail(lua, "SipiImage.exif(): Unrecognized EXIF-Tag");
    };
    let img = ud.borrow::<LuaImage>()?;
    let outcome = img.handle.borrow().exif(&tag);
    match outcome {
        ExifValue::Json(json) => {
            let parsed: serde_json::Value = serde_json::from_str(&json)
                .map_err(|e| mlua::Error::runtime(format!("exif JSON: {e}")))?;
            ok2(exif_json_to_lua(lua, &parsed)?)
        }
        ExifValue::Unrecognized => fail(lua, "SipiImage.exif(): Unrecognized EXIF-Tag"),
        ExifValue::Unavailable => fail(lua, "SipiImage.exif(): requested exif tag not available"),
        ExifValue::NoExifData => fail(lua, "SipiImage.exif(): no exif data available"),
        ExifValue::Internal => fail(lua, "SipiImage.exif(): internal engine error"),
    }
}

/// EXIF JSON → the historical Lua shapes: scalars as-is, arrays (rationals
/// included) as 1-based tables.
fn exif_json_to_lua(lua: &Lua, value: &serde_json::Value) -> mlua::Result<Value> {
    Ok(match value {
        serde_json::Value::String(s) => Value::String(lua.create_string(s)?),
        serde_json::Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                Value::Integer(i)
            } else {
                Value::Number(n.as_f64().unwrap_or(f64::NAN))
            }
        }
        serde_json::Value::Array(items) => {
            let t = lua.create_table()?;
            for (i, item) in items.iter().enumerate() {
                t.set(i + 1, exif_json_to_lua(lua, item)?)?;
            }
            Value::Table(t)
        }
        other => Value::String(lua.create_string(other.to_string())?),
    })
}

fn image_gps(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() != 1 {
        return fail(lua, "SipiImage.gps(): Incorrect number of arguments");
    }
    let ud = match image_arg(lua, &args, "gps") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let img = ud.borrow::<LuaImage>()?;
    let outcome = img.handle.borrow().gps();
    match outcome {
        GpsValue::Internal => fail(lua, "SipiImage.gps(): internal engine error"),
        GpsValue::NoExifData => fail(lua, "SipiImage.gps(): no exif data available"),
        GpsValue::Json(json) => {
            let parsed: serde_json::Value = serde_json::from_str(&json)
                .map_err(|e| mlua::Error::runtime(format!("gps JSON: {e}")))?;
            let out = lua.create_table()?;
            if let serde_json::Value::Object(map) = parsed {
                for (key, value) in map {
                    match value {
                        // GPS coordinate triples keep the historical 0-based
                        // element indices.
                        serde_json::Value::Array(items) => {
                            let t = lua.create_table()?;
                            for (i, item) in items.iter().enumerate() {
                                t.set(i, item.as_f64().unwrap_or(0.0))?;
                            }
                            out.set(key, t)?;
                        }
                        serde_json::Value::String(s) => out.set(key, s)?,
                        serde_json::Value::Number(n) => out.set(key, n.as_f64().unwrap_or(0.0))?,
                        _ => {}
                    }
                }
            }
            ok2(Value::Table(out))
        }
    }
}

fn image_crop(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let ud = match image_arg(lua, &args, "crop") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let Some(region) = args.get(1).and_then(coerce_string) else {
        return fail(lua, "SipiImage.crop(): Incorrect number of arguments");
    };
    let img = ud.borrow::<LuaImage>()?;
    let outcome = img.handle.borrow_mut().crop(&region);
    match outcome {
        Err(msg) => fail(lua, format!("SipiImage.crop(): {msg}")),
        Ok(()) => ok_nil(),
    }
}

fn image_scale(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let ud = match image_arg(lua, &args, "scale") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let Some(size) = args.get(1).and_then(coerce_string) else {
        return fail(lua, "SipiImage.scale(): Incorrect number of arguments");
    };
    let img = ud.borrow::<LuaImage>()?;
    let outcome = img.handle.borrow_mut().scale(&size);
    match outcome {
        Err(msg) => fail(lua, format!("SipiImage.scale(): {msg}")),
        Ok(()) => ok_nil(),
    }
}

fn image_rotate(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() < 2 || args.len() > 3 {
        return fail(lua, "SipiImage.rotate(): Incorrect number of arguments");
    }
    let ud = match image_arg(lua, &args, "rotate") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let angle = match &args[1] {
        Value::Integer(i) => *i as f32,
        Value::Number(n) => *n as f32,
        _ => return fail(lua, "SipiImage.rotate(): Incorrect  arguments"),
    };
    let mut mirror = false;
    if let Some(third) = args.get(2) {
        let Value::Boolean(b) = third else {
            return fail(lua, "IIIFImage.rotate(): Incorrect  argument for mirror");
        };
        mirror = *b;
    }
    let img = ud.borrow::<LuaImage>()?;
    let outcome = img.handle.borrow_mut().rotate(angle, mirror);
    match outcome {
        Err(msg) => fail(lua, format!("SipiImage.rotate(): {msg}")),
        Ok(()) => ok_nil(),
    }
}

fn image_topleft(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() != 1 {
        return fail(lua, "SipiImage.topleft(): Incorrect number of arguments");
    }
    let ud = match image_arg(lua, &args, "topleft") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let img = ud.borrow::<LuaImage>()?;
    let _ = img.handle.borrow_mut().topleft();
    // Historical single bare `true` return (not the (true, nil) pair).
    Ok(MultiValue::from_iter([Value::Boolean(true)]))
}

fn image_watermark(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    let ud = match image_arg(lua, &args, "watermark") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let Some(wmfile) = args.get(1).and_then(coerce_string) else {
        return fail(lua, "SipiImage.watermark(): Incorrect arguments");
    };
    let img = ud.borrow::<LuaImage>()?;
    let outcome = img.handle.borrow_mut().watermark(&wmfile);
    match outcome {
        Err(msg) => fail(lua, format!("SipiImage.watermark(): {msg}")),
        Ok(()) => ok_nil(),
    }
}

fn image_mimetype_consistency(lua: &Lua, args: Variadic<Value>) -> mlua::Result<MultiValue> {
    if args.len() != 3 {
        return fail(
            lua,
            "SipiImage.mimetype_consistency(): Incorrect number of arguments",
        );
    }
    let ud = match image_arg(lua, &args, "mimetype_consistency") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let mimetype = args.get(1).and_then(coerce_string).unwrap_or_default();
    let filename = args.get(2).and_then(coerce_string).unwrap_or_default();
    let img = ud.borrow::<LuaImage>()?;
    let outcome = img
        .handle
        .borrow()
        .mimetype_consistency(&mimetype, &filename);
    match outcome {
        Err(msg) => fail(lua, format!("SipiImage.mimetype_consistency(): {msg}")),
        Ok(check) => ok2(Value::Boolean(check)),
    }
}

const SPROFILE_VALUES: [&str; 11] = [
    "PROFILE0",
    "PROFILE1",
    "PROFILE2",
    "PART2",
    "CINEMA2K",
    "CINEMA4K",
    "BROADCAST",
    "CINEMA2S",
    "CINEMA4S",
    "CINEMASS",
    "IMF",
];
const CORDER_VALUES: [&str; 5] = ["LRCP", "RLCP", "RPCL", "PCRL", "CPRL"];

enum ParamsOutcome {
    Ok {
        params: Vec<(String, String)>,
        file_role: Option<String>,
        origname: Option<String>,
        mimetype: Option<String>,
    },
    Fail(String),
    Raise(String),
}

/// The `write` compression-parameter validation, value semantics preserved:
/// non-string names/values are `(false, msg)`, invalid values for the
/// validated enums raise (lua_error convention), unknown keys raise.
fn validate_write_params(table: &Table) -> mlua::Result<ParamsOutcome> {
    let mut params: Vec<(String, String)> = Vec::new();
    let mut file_role = None;
    let mut origname = None;
    let mut mimetype = None;
    for pair in table.pairs::<Value, Value>() {
        let (key, value) = pair?;
        let Some(key) = coerce_string(&key) else {
            return Ok(ParamsOutcome::Fail(
                "SipiImage.write(): Incorrect compression value name: Must be string!".into(),
            ));
        };
        let Some(value) = coerce_string(&value) else {
            return Ok(ParamsOutcome::Fail(
                "SipiImage.write(): Incorrect compression value: Must be string!".into(),
            ));
        };
        match key.as_str() {
            "Sprofile" => {
                if !SPROFILE_VALUES.contains(&value.as_str()) {
                    return Ok(ParamsOutcome::Raise(
                        "SipiImage.write(): invalid Sprofile!".into(),
                    ));
                }
                params.push((key, value));
            }
            "Creversible" | "Cuse_sop" => {
                if value != "yes" && value != "no" {
                    return Ok(ParamsOutcome::Raise(format!(
                        "SipiImage.write(): invalid {key}!"
                    )));
                }
                params.push((key, value));
            }
            "Clayers" | "Clevels" => match value.trim().parse::<i32>() {
                Ok(i) => params.push((key, i.to_string())),
                Err(_) => {
                    return Ok(ParamsOutcome::Raise(format!(
                        "SipiImage.write(): invalid {key}!"
                    )));
                }
            },
            "Corder" => {
                if !CORDER_VALUES.contains(&value.as_str()) {
                    return Ok(ParamsOutcome::Raise(
                        "SipiImage.write(): invalid Corder!".into(),
                    ));
                }
                params.push((key, value));
            }
            "Cprecincts" | "Cblk" | "rates" | "quality" => params.push((key, value)),
            "file_role" => {
                if value != "service-file" {
                    return Ok(ParamsOutcome::Raise(
                        "SipiImage.write(): file_role must be \"service-file\" (other roles not yet implemented)."
                            .into(),
                    ));
                }
                file_role = Some(value);
            }
            "origname" => origname = Some(value),
            "mimetype" => mimetype = Some(value),
            _ => {
                return Ok(ParamsOutcome::Raise(
                    "SipiImage.write(): invalid compression parameter!".into(),
                ));
            }
        }
    }
    Ok(ParamsOutcome::Ok {
        params,
        file_role,
        origname,
        mimetype,
    })
}

fn map_extension(extension: &str, allow_jp2: bool) -> Option<&'static str> {
    match extension {
        "tif" | "tiff" => Some("tif"),
        "jpg" | "jpeg" => Some("jpg"),
        "png" => Some("png"),
        "j2k" | "jpx" => Some("jpx"),
        // write() accepts jp2; send() historically does not.
        "jp2" if allow_jp2 => Some("jpx"),
        _ => None,
    }
}

fn image_write(
    lua: &Lua,
    resp: &Rc<RefCell<ResponseWriter>>,
    args: Variadic<Value>,
) -> mlua::Result<MultiValue> {
    let ud = match image_arg(lua, &args, "write") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let Some(Value::String(path)) = args.get(1) else {
        return fail(lua, "SipiImage.write(): Incorrect arguments");
    };
    let imgpath = path.to_string_lossy();

    let (params, file_role, origname, mimetype) = if let Some(Value::Table(t)) = args.get(2) {
        match validate_write_params(t)? {
            ParamsOutcome::Fail(msg) => return fail(lua, msg),
            ParamsOutcome::Raise(msg) => return Err(mlua::Error::runtime(msg)),
            ParamsOutcome::Ok {
                params,
                file_role,
                origname,
                mimetype,
            } => (params, file_role, origname, mimetype),
        }
    } else {
        (Vec::new(), None, None, None)
    };

    // Path decomposition mirrors the historical split: the basename between
    // the last '/' and the last '.', the extension after the last '.'.
    let (basename, extension) = {
        let start = imgpath.rfind('/').map_or(0, |i| i + 1);
        match imgpath.rfind('.') {
            Some(dot) if dot >= start => (
                &imgpath[start..dot],
                imgpath[dot + 1..].to_ascii_lowercase(),
            ),
            _ => (&imgpath[start..], String::new()),
        }
    };
    let Some(ftype) = map_extension(&extension, true) else {
        return Err(mlua::Error::runtime(
            "SipiImage.write(): unsupported file format",
        ));
    };

    let essentials: Option<(String, String)> = if file_role.is_some() {
        let (Some(origname), Some(mimetype)) = (origname, mimetype) else {
            return Err(mlua::Error::runtime(
                "SipiImage.write(): file_role=\"service-file\" requires both origname and mimetype.",
            ));
        };
        if ftype != "jpx" && ftype != "tif" {
            return Err(mlua::Error::runtime(
                "SipiImage.write(): file_role=\"service-file\" requires JP2 or pyramidal TIFF output \
                 (Service Files only live in those two carriers per ADR-0009).",
            ));
        }
        Some((origname, mimetype))
    } else {
        None
    };
    let essentials_ref = essentials.as_ref().map(|(o, m)| (o.as_str(), m.as_str()));

    let img = ud.borrow::<LuaImage>()?;
    // The "http"/"HTTP" basename (exact, case-sensitive pair) streams the
    // encoded bytes to the response sink instead of writing a file.
    if basename == "http" || basename == "HTTP" {
        if essentials_ref.is_some() {
            // Service-file stamping writes a file; combining it with the
            // streaming basename was never meaningful. Stream without it.
        }
        let mut writer = resp.borrow_mut();
        let outcome = img
            .handle
            .borrow_mut()
            .send(ftype, &params, |chunk| writer.write(chunk).is_ok());
        return match outcome {
            Err(msg) => fail(lua, msg),
            Ok(()) => {
                drop(writer);
                ok2(Value::String(lua.create_string(imgpath.as_bytes())?))
            }
        };
    }

    let outcome = img
        .handle
        .borrow_mut()
        .write(ftype, &imgpath, &params, essentials_ref);
    match outcome {
        Err(msg) => fail(lua, msg),
        Ok(()) => ok2(Value::String(lua.create_string(imgpath.as_bytes())?)),
    }
}

fn image_send(
    lua: &Lua,
    resp: &Rc<RefCell<ResponseWriter>>,
    args: Variadic<Value>,
) -> mlua::Result<MultiValue> {
    let ud = match image_arg(lua, &args, "send") {
        Ok(ud) => ud,
        Err(ret) => return ret,
    };
    let Some(ext) = args.get(1).and_then(coerce_string) else {
        return fail(lua, "SipiImage.send(): Incorrect arguments");
    };
    let Some(ftype) = map_extension(&ext.to_ascii_lowercase(), false) else {
        return Err(mlua::Error::runtime(
            "SipiImage.send(): unsupported file format",
        ));
    };
    let img = ud.borrow::<LuaImage>()?;
    let mut writer = resp.borrow_mut();
    let outcome = img
        .handle
        .borrow_mut()
        .send(ftype, &[], |chunk| writer.write(chunk).is_ok());
    match outcome {
        Err(msg) => fail(lua, msg),
        Ok(()) => ok_nil(),
    }
}
