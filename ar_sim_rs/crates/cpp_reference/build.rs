//! Compiles the C++ reference codec (`cot_codec.cpp` from the ar_sim tree) and the C shim.
//! AR_SIM_CPP names the ar_sim source directory; the default is the sibling `ar_sim`.

use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let source = std::env::var_os("AR_SIM_CPP")
        .map(PathBuf::from)
        .unwrap_or_else(|| manifest.join("../../../ar_sim"));
    let codec = source.join("cot_codec.cpp");
    assert!(
        codec.is_file(),
        "no C++ reference at {}: set AR_SIM_CPP to the ar_sim source directory",
        codec.display()
    );
    cc::Build::new()
        .cpp(true)
        .std("c++17")
        .opt_level(2)
        .include(&source)
        .file(&codec)
        .file(manifest.join("shim/ref_shim.cpp"))
        .compile("ar_sim_reference");
    println!("cargo:rerun-if-changed={}", codec.display());
    println!(
        "cargo:rerun-if-changed={}",
        source.join("cot_codec.hpp").display()
    );
    println!("cargo:rerun-if-changed=shim/ref_shim.cpp");
    println!("cargo:rerun-if-env-changed=AR_SIM_CPP");
}
