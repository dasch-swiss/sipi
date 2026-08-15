//! IIIF Image API 3.0 URL parser.
//!
//! Classifies a request URI and, for an IIIF image request, parses the
//! `{region}/{size}/{rotation}/{quality}.{format}` path into the domain
//! [`IiifParams`]. A pure-CPU parser with a single dependency
//! (`percent-encoding`); consumers own the mapping from the emitted domain types
//! into their own representation.
//!
//! Correctness is gated by the unit tests colocated with each module, the
//! corpus regression sweep (`corpus_regression_test`), and the e2e suite
//! (`proptest_iiif_uri`, `iiif_compliance`).

mod domain;
mod parse;
mod request;

pub use domain::{FormatKind, IiifParams, ParseError, QualityKind, RegionKind, SizeKind};
pub use request::{parse_request, ParsedRequest, RequestKind};
