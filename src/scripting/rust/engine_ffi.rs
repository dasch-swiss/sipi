//! Hand-mirrored `extern "C"` declarations and safe wrappers for the
//! `sipi_image_*` handle family (`src/ffi/sipi_ffi.h` — the contract lives on
//! the C declarations). The wrappers own the handle ([`ImageHandle`] frees it
//! in `Drop`, so a killed/unwound VM releases every handle), deep-copy every
//! emitted string inside the callback, and `catch_unwind`-wrap the one Rust
//! callback that runs inside C++ codec frames ([`ImageHandle::send`]).

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

pub type SipiStrFn = extern "C" fn(ctx: *mut c_void, value: *const c_char);
pub type SipiKVFn = extern "C" fn(ctx: *mut c_void, key: *const c_char, value: *const c_char);
pub type SipiWriteFn = extern "C" fn(ctx: *mut c_void, data: *const u8, len: usize) -> c_int;

#[repr(C)]
struct SipiImageHandle {
    _opaque: [u8; 0],
}

extern "C" {
    fn sipi_image_new(
        path: *const c_char,
        region: *const c_char,
        size: *const c_char,
        reduce: c_int,
        has_reduce: c_int,
        original: *const c_char,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> *mut SipiImageHandle;
    fn sipi_image_free(img: *mut SipiImageHandle);
    fn sipi_image_handle_dims(
        img: *const SipiImageHandle,
        nx: *mut u64,
        ny: *mut u64,
        orientation: *mut c_int,
    ) -> c_int;
    fn sipi_image_file_dims(
        path: *const c_char,
        nx: *mut u64,
        ny: *mut u64,
        orientation: *mut c_int,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_crop(
        img: *mut SipiImageHandle,
        iiif_region: *const c_char,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_scale(
        img: *mut SipiImageHandle,
        iiif_size: *const c_char,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_rotate(
        img: *mut SipiImageHandle,
        angle: f32,
        mirror: c_int,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_topleft(img: *mut SipiImageHandle) -> c_int;
    fn sipi_image_watermark(
        img: *mut SipiImageHandle,
        wmfile: *const c_char,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_exif_get(
        img: *const SipiImageHandle,
        tag: *const c_char,
        emit: SipiStrFn,
        ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_gps(img: *const SipiImageHandle, emit: SipiStrFn, ctx: *mut c_void) -> c_int;
    fn sipi_image_mimetype_consistency(
        img: *const SipiImageHandle,
        mimetype: *const c_char,
        filename: *const c_char,
        consistent: *mut c_int,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_write(
        img: *mut SipiImageHandle,
        ftype: *const c_char,
        path: *const c_char,
        param_keys: *const *const c_char,
        param_values: *const *const c_char,
        n_params: usize,
        origname: *const c_char,
        mimetype: *const c_char,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_send(
        img: *mut SipiImageHandle,
        ftype: *const c_char,
        param_keys: *const *const c_char,
        param_values: *const *const c_char,
        n_params: usize,
        write: SipiWriteFn,
        write_ctx: *mut c_void,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_image_tostring(img: *const SipiImageHandle, emit: SipiStrFn, ctx: *mut c_void)
        -> c_int;
    fn sipi_filename_hash(filename: *const c_char, emit: SipiStrFn, ctx: *mut c_void) -> c_int;
    fn sipi_file_mimetype(
        path: *const c_char,
        emit: SipiKVFn,
        ctx: *mut c_void,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
    fn sipi_file_mimeconsistency(
        path: *const c_char,
        filename: *const c_char,
        expected_mimetype: *const c_char,
        consistent: *mut c_int,
        err: SipiStrFn,
        err_ctx: *mut c_void,
    ) -> c_int;
}

/// Collects one emitted string into the `Option<String>` at `ctx` — the emit
/// pointer is only valid during the call, so it is deep-copied immediately.
extern "C" fn collect_string(ctx: *mut c_void, value: *const c_char) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if value.is_null() {
            return;
        }
        // SAFETY: `ctx` is the `&mut Option<String>` this crate passed in;
        // `value` is a NUL-terminated C string valid for the call.
        let out = unsafe { &mut *(ctx as *mut Option<String>) };
        // SAFETY: as above.
        *out = Some(
            unsafe { CStr::from_ptr(value) }
                .to_string_lossy()
                .into_owned(),
        );
    }));
}

/// Collects emitted key/value pairs into the `Vec` at `ctx`.
extern "C" fn collect_kv(ctx: *mut c_void, key: *const c_char, value: *const c_char) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if key.is_null() || value.is_null() {
            return;
        }
        // SAFETY: `ctx` is the `&mut Vec<(String, String)>` this crate passed
        // in; both pointers are NUL-terminated C strings valid for the call.
        let out = unsafe { &mut *(ctx as *mut Vec<(String, String)>) };
        // SAFETY: as above.
        let (k, v) = unsafe {
            (
                CStr::from_ptr(key).to_string_lossy().into_owned(),
                CStr::from_ptr(value).to_string_lossy().into_owned(),
            )
        };
        out.push((k, v));
    }));
}

/// The body-write trampoline for [`ImageHandle::send`]: `ctx` is the
/// `&mut dyn FnMut(&[u8]) -> bool` sink closure. `catch_unwind` keeps a Rust
/// panic from unwinding through the C++ codec frames — a panic reports write
/// failure (non-zero), which makes the codec abort cleanly.
extern "C" fn write_trampoline(ctx: *mut c_void, data: *const u8, len: usize) -> c_int {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if data.is_null() || len == 0 {
            return 0;
        }
        // SAFETY: `ctx` is the `&mut &mut dyn FnMut` this crate passed in;
        // the engine guarantees `data` points at `len` valid bytes.
        let sink = unsafe { &mut *(ctx as *mut &mut dyn FnMut(&[u8]) -> bool) };
        // SAFETY: as above.
        let chunk = unsafe { std::slice::from_raw_parts(data, len) };
        if sink(chunk) {
            0
        } else {
            1
        }
    }))
    .unwrap_or(1)
}

fn cstr(s: &str) -> CString {
    // Interior NULs cannot round-trip a C string; the affected byte sequence
    // is replaced rather than panicking on script-controlled input.
    CString::new(s).unwrap_or_else(|_| CString::new(s.replace('\0', "\u{FFFD}")).expect("NUL-free"))
}

fn opt_cstr(s: Option<&str>) -> Option<CString> {
    s.map(cstr)
}

fn ptr_of(c: &Option<CString>) -> *const c_char {
    c.as_ref().map_or(std::ptr::null(), |c| c.as_ptr())
}

/// The GPS block read outcome.
pub enum GpsValue {
    /// The emitted JSON object.
    Json(String),
    NoExifData,
    Internal,
}

/// The typed outcome of an EXIF tag read.
pub enum ExifValue {
    /// The tag's value as the emitted JSON document.
    Json(String),
    Unrecognized,
    Unavailable,
    NoExifData,
    Internal,
}

/// An owned engine image handle (`SipiImage` engine-side). Freed on `Drop`.
pub struct ImageHandle {
    raw: *mut SipiImageHandle,
}

impl Drop for ImageHandle {
    fn drop(&mut self) {
        // SAFETY: `raw` is the pointer sipi_image_new returned, freed exactly
        // once here (the struct is not Clone/Copy).
        unsafe { sipi_image_free(self.raw) };
    }
}

/// Marshal `params` into parallel C-string arrays and call `f` with the
/// borrowed pointers (valid for the call only — the engine deep-copies).
fn with_params<R>(
    params: &[(String, String)],
    f: impl FnOnce(*const *const c_char, *const *const c_char, usize) -> R,
) -> R {
    let keys: Vec<CString> = params.iter().map(|(k, _)| cstr(k)).collect();
    let values: Vec<CString> = params.iter().map(|(_, v)| cstr(v)).collect();
    let key_ptrs: Vec<*const c_char> = keys.iter().map(|c| c.as_ptr()).collect();
    let value_ptrs: Vec<*const c_char> = values.iter().map(|c| c.as_ptr()).collect();
    f(key_ptrs.as_ptr(), value_ptrs.as_ptr(), params.len())
}

impl ImageHandle {
    /// Reads an image (optionally IIIF-region/size-shaped or reduced; a
    /// non-empty `original` selects the preservation-aware read).
    pub fn new(
        path: &str,
        region: Option<&str>,
        size: Option<&str>,
        reduce: Option<i64>,
        original: Option<&str>,
    ) -> Result<Self, String> {
        let c_path = cstr(path);
        let c_region = opt_cstr(region);
        let c_size = opt_cstr(size);
        let c_original = opt_cstr(original);
        let mut err: Option<String> = None;
        // SAFETY: all pointers outlive the synchronous call; the engine
        // deep-copies inputs and the error callback copies its message.
        let raw = unsafe {
            sipi_image_new(
                c_path.as_ptr(),
                ptr_of(&c_region),
                ptr_of(&c_size),
                reduce
                    .unwrap_or(0)
                    .clamp(c_int::MIN as i64, c_int::MAX as i64) as c_int,
                c_int::from(reduce.is_some()),
                ptr_of(&c_original),
                collect_string,
                (&mut err) as *mut Option<String> as *mut c_void,
            )
        };
        if raw.is_null() {
            return Err(err.unwrap_or_else(|| "unknown engine error".to_string()));
        }
        Ok(Self { raw })
    }

    pub fn dims(&self) -> Result<(u64, u64, i32), String> {
        let (mut nx, mut ny, mut orientation) = (0u64, 0u64, 0 as c_int);
        // SAFETY: the out-pointers are valid; the handle is live (owned).
        let code = unsafe { sipi_image_handle_dims(self.raw, &mut nx, &mut ny, &mut orientation) };
        if code != 0 {
            return Err("internal engine error".to_string());
        }
        Ok((nx, ny, orientation))
    }

    pub fn crop(&mut self, iiif_region: &str) -> Result<(), String> {
        let c_region = cstr(iiif_region);
        let mut err: Option<String> = None;
        // SAFETY: pointers valid for the call; error message deep-copied.
        let code = unsafe {
            sipi_image_crop(
                self.raw,
                c_region.as_ptr(),
                collect_string,
                (&mut err) as *mut Option<String> as *mut c_void,
            )
        };
        status_to_result(code, err)
    }

    pub fn scale(&mut self, iiif_size: &str) -> Result<(), String> {
        let c_size = cstr(iiif_size);
        let mut err: Option<String> = None;
        // SAFETY: pointers valid for the call; error message deep-copied.
        let code = unsafe {
            sipi_image_scale(
                self.raw,
                c_size.as_ptr(),
                collect_string,
                (&mut err) as *mut Option<String> as *mut c_void,
            )
        };
        status_to_result(code, err)
    }

    pub fn rotate(&mut self, angle: f32, mirror: bool) -> Result<(), String> {
        let mut err: Option<String> = None;
        // SAFETY: pointers valid for the call.
        let code = unsafe {
            sipi_image_rotate(
                self.raw,
                angle,
                c_int::from(mirror),
                collect_string,
                (&mut err) as *mut Option<String> as *mut c_void,
            )
        };
        status_to_result(code, err)
    }

    pub fn topleft(&mut self) -> Result<(), String> {
        // SAFETY: the handle is live.
        let code = unsafe { sipi_image_topleft(self.raw) };
        status_to_result(code, None)
    }

    pub fn watermark(&mut self, wmfile: &str) -> Result<(), String> {
        let c_wm = cstr(wmfile);
        let mut err: Option<String> = None;
        // SAFETY: pointers valid for the call; error message deep-copied.
        let code = unsafe {
            sipi_image_watermark(
                self.raw,
                c_wm.as_ptr(),
                collect_string,
                (&mut err) as *mut Option<String> as *mut c_void,
            )
        };
        status_to_result(code, err)
    }

    pub fn exif(&self, tag: &str) -> ExifValue {
        let c_tag = cstr(tag);
        let mut json: Option<String> = None;
        // SAFETY: pointers valid for the call; the emitted JSON is deep-copied.
        let code = unsafe {
            sipi_image_exif_get(
                self.raw,
                c_tag.as_ptr(),
                collect_string,
                (&mut json) as *mut Option<String> as *mut c_void,
            )
        };
        match (code, json) {
            (0, Some(j)) => ExifValue::Json(j),
            (1, _) => ExifValue::Unrecognized,
            (2, _) => ExifValue::Unavailable,
            (3, _) => ExifValue::NoExifData,
            _ => ExifValue::Internal,
        }
    }

    pub fn gps(&self) -> GpsValue {
        let mut json: Option<String> = None;
        // SAFETY: pointers valid for the call; the emitted JSON is deep-copied.
        let code = unsafe {
            sipi_image_gps(
                self.raw,
                collect_string,
                (&mut json) as *mut Option<String> as *mut c_void,
            )
        };
        match (code, json) {
            (0, Some(j)) => GpsValue::Json(j),
            (3, _) => GpsValue::NoExifData,
            _ => GpsValue::Internal,
        }
    }

    pub fn mimetype_consistency(&self, mimetype: &str, filename: &str) -> Result<bool, String> {
        let c_mime = cstr(mimetype);
        let c_name = cstr(filename);
        let mut consistent: c_int = 0;
        let mut err: Option<String> = None;
        // SAFETY: pointers valid for the call; error message deep-copied.
        let code = unsafe {
            sipi_image_mimetype_consistency(
                self.raw,
                c_mime.as_ptr(),
                c_name.as_ptr(),
                &mut consistent,
                collect_string,
                (&mut err) as *mut Option<String> as *mut c_void,
            )
        };
        status_to_result(code, err).map(|()| consistent != 0)
    }

    /// Encode + write to a file; a `Some((origname, mimetype))` requests
    /// Service-File stamping (Essentials packet built engine-side).
    pub fn write(
        &mut self,
        ftype: &str,
        path: &str,
        params: &[(String, String)],
        essentials: Option<(&str, &str)>,
    ) -> Result<(), String> {
        let c_ftype = cstr(ftype);
        let c_path = cstr(path);
        let c_origname = opt_cstr(essentials.map(|(o, _)| o));
        let c_mimetype = opt_cstr(essentials.map(|(_, m)| m));
        let mut err: Option<String> = None;
        with_params(params, |keys, values, n| {
            // SAFETY: every pointer outlives the synchronous call; the engine
            // deep-copies inputs and the error callback copies its message.
            let code = unsafe {
                sipi_image_write(
                    self.raw,
                    c_ftype.as_ptr(),
                    c_path.as_ptr(),
                    keys,
                    values,
                    n,
                    ptr_of(&c_origname),
                    ptr_of(&c_mimetype),
                    collect_string,
                    (&mut err) as *mut Option<String> as *mut c_void,
                )
            };
            status_to_result(code, err.take())
        })
    }

    /// Encode + stream through `sink` (return `false` from the sink to abort).
    pub fn send(
        &mut self,
        ftype: &str,
        params: &[(String, String)],
        mut sink: impl FnMut(&[u8]) -> bool,
    ) -> Result<(), String> {
        let c_ftype = cstr(ftype);
        let mut err: Option<String> = None;
        let mut sink_dyn: &mut dyn FnMut(&[u8]) -> bool = &mut sink;
        with_params(params, |keys, values, n| {
            // SAFETY: every pointer outlives the synchronous call; the write
            // trampoline is catch_unwind-wrapped so no panic crosses the
            // codec frames; the error callback copies its message.
            let code = unsafe {
                sipi_image_send(
                    self.raw,
                    c_ftype.as_ptr(),
                    keys,
                    values,
                    n,
                    write_trampoline,
                    (&mut sink_dyn) as *mut &mut dyn FnMut(&[u8]) -> bool as *mut c_void,
                    collect_string,
                    (&mut err) as *mut Option<String> as *mut c_void,
                )
            };
            status_to_result(code, err.take())
        })
    }

    pub fn to_display_string(&self) -> String {
        let mut out: Option<String> = None;
        // SAFETY: pointers valid for the call; the emitted string is copied.
        let code = unsafe {
            sipi_image_tostring(
                self.raw,
                collect_string,
                (&mut out) as *mut Option<String> as *mut c_void,
            )
        };
        if code == 0 {
            out.unwrap_or_default()
        } else {
            String::new()
        }
    }
}

fn status_to_result(code: c_int, err: Option<String>) -> Result<(), String> {
    if code == 0 {
        Ok(())
    } else {
        Err(err.unwrap_or_else(|| "internal engine error".to_string()))
    }
}

/// Header-only shape probe for a path (the `SipiImage.dims(path)` form).
pub fn file_dims(path: &str) -> Result<(u64, u64, i32), String> {
    let c_path = cstr(path);
    let (mut nx, mut ny, mut orientation) = (0u64, 0u64, 0 as c_int);
    let mut err: Option<String> = None;
    // SAFETY: pointers valid for the call; error message deep-copied.
    let code = unsafe {
        sipi_image_file_dims(
            c_path.as_ptr(),
            &mut nx,
            &mut ny,
            &mut orientation,
            collect_string,
            (&mut err) as *mut Option<String> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(err.unwrap_or_else(|| "internal engine error".to_string()));
    }
    Ok((nx, ny, orientation))
}

/// `helper.filename_hash` — the byte-identical storage-path derivation.
pub fn filename_hash(filename: &str) -> Result<String, String> {
    let c_name = cstr(filename);
    let mut out: Option<String> = None;
    // SAFETY: pointers valid for the call; the emitted string is copied.
    let code = unsafe {
        sipi_filename_hash(
            c_name.as_ptr(),
            collect_string,
            (&mut out) as *mut Option<String> as *mut c_void,
        )
    };
    let text = out.unwrap_or_default();
    if code == 0 {
        Ok(text)
    } else {
        Err(text)
    }
}

/// `server.file_mimetype` — libmagic sniff.
pub fn file_mimetype(path: &str) -> Result<(String, Option<String>), String> {
    let c_path = cstr(path);
    let mut kv: Vec<(String, String)> = Vec::new();
    let mut err: Option<String> = None;
    // SAFETY: pointers valid for the call; emitted pairs are deep-copied.
    let code = unsafe {
        sipi_file_mimetype(
            c_path.as_ptr(),
            collect_kv,
            (&mut kv) as *mut Vec<(String, String)> as *mut c_void,
            collect_string,
            (&mut err) as *mut Option<String> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(err.unwrap_or_else(|| "internal engine error".to_string()));
    }
    let mut mimetype = String::new();
    let mut charset = None;
    for (k, v) in kv {
        match k.as_str() {
            "mimetype" => mimetype = v,
            "charset" => charset = Some(v),
            _ => {}
        }
    }
    Ok((mimetype, charset))
}

/// `server.file_mimeconsistency`.
pub fn file_mimeconsistency(
    path: &str,
    filename: &str,
    expected_mimetype: &str,
) -> Result<bool, String> {
    let c_path = cstr(path);
    let c_name = cstr(filename);
    let c_mime = cstr(expected_mimetype);
    let mut consistent: c_int = 0;
    let mut err: Option<String> = None;
    // SAFETY: pointers valid for the call; error message deep-copied.
    let code = unsafe {
        sipi_file_mimeconsistency(
            c_path.as_ptr(),
            c_name.as_ptr(),
            c_mime.as_ptr(),
            &mut consistent,
            collect_string,
            (&mut err) as *mut Option<String> as *mut c_void,
        )
    };
    if code != 0 {
        return Err(err.unwrap_or_else(|| "internal engine error".to_string()));
    }
    Ok(consistent != 0)
}
