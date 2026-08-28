//! The discoverable command list. Each Rust-native verb is its own module
//! (mirroring the C++ `src/cli/commands/` convention); every other verb is
//! forwarded to the C++ CLI by `main`.

pub mod health;
pub mod server;
