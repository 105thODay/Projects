//! Generates Rust types from the TAK Protocol v1 .proto files (ATAK-CIV `takproto`).
//! TAKPROTO_DIR names the directory; the default is a sibling checkout at
//! ../tak_src/repo/takproto. Without it the crate builds empty and its tests do not run.

use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-env-changed=TAKPROTO_DIR");
    println!("cargo::rustc-check-cfg=cfg(takproto)");
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let dir = std::env::var_os("TAKPROTO_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| manifest.join("../../../tak_src/repo/takproto"));
    let root = dir.join("takmessage.proto");
    if !root.is_file() {
        println!(
            "cargo:warning=no TAK .proto files at {}: set TAKPROTO_DIR; the prost oracle is off",
            dir.display()
        );
        return;
    }
    let protoc = protoc_bin_vendored::protoc_bin_path().expect("vendored protoc");
    prost_build::Config::new()
        .protoc_executable(protoc)
        .compile_protos(&[root], std::slice::from_ref(&dir))
        .expect("TAK protos compile");
    println!("cargo:rerun-if-changed={}", dir.display());
    println!("cargo:rustc-cfg=takproto");
}
