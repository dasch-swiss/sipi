//! Domain types the parser emits.
//!
//! These are idiomatic Rust value types (`bool` flags, plain enums, no
//! `#[repr(C)]`). They are total supersets of the classification the parser
//! admits: `parse_quality_format` produces `Gif`/`Pdf`/`Webp`/`Unsupported`
//! formats and `parse_size` carries a `Reduce` branch, even though the
//! classifier rejects those forms — the constructors mirror the C++ value
//! objects (`//src/iiifparser/cpp/value_objects`) branch-for-branch. Consumers
//! own the mapping from these types into their own representation: the FFI seam
//! flattening lives in `server-rs` (`src/server-rs/src/ffi.rs`, the
//! `From<IiifParams> for SipiIiifParams` impls), so adding a variant to any enum
//! here requires updating those exhaustive matches too (a new variant fails to
//! compile until it is mapped).

/// A parse failure → HTTP 400 at the edge.
#[derive(Debug, PartialEq, Eq)]
pub struct ParseError(pub String);

/// The IIIF region kind (`{region}` path segment).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RegionKind {
    Full,
    Square,
    Coords,
    Percents,
}

/// The IIIF size kind (`{size}` path segment). `Undefined` is the unset default
/// and `Reduce` covers the unreachable `red:` branch kept for constructor
/// fidelity with the C++ `SipiSize`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SizeKind {
    Undefined,
    Full,
    PixelsXy,
    PixelsX,
    PixelsY,
    Maxdim,
    Percents,
    Reduce,
}

/// The IIIF quality kind (`{quality}` of the `quality.format` tail).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QualityKind {
    Default,
    Color,
    Gray,
    Bitonal,
}

/// The IIIF output format (`{format}` of the `quality.format` tail).
/// `Unsupported` and `Gif`/`Pdf`/`Webp` are produced by `parse_quality_format`
/// even though classification rejects them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FormatKind {
    Unsupported,
    Jpg,
    Tif,
    Png,
    Gif,
    Jp2,
    Pdf,
    Webp,
}

/// The parsed IIIF `{region}/{size}/{rotation}/{quality}.{format}` parameters.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct IiifParams {
    pub region_kind: RegionKind,
    pub region: [f32; 4],
    pub size_kind: SizeKind,
    pub size_upscaling: bool,
    pub size_percent: f32,
    pub size_reduce: i32,
    pub size_nx: usize,
    pub size_ny: usize,
    pub rotation: f32,
    pub rotation_mirror: bool,
    pub quality_kind: QualityKind,
    pub format_kind: FormatKind,
}
