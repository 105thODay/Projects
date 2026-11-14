//! Gate: every stream reproduces the first 10,000 raw draws the C++ reference recorded for
//! run seeds 1 to 3 (rng.jsonl in the golden corpus), from the same derived seed.

use std::path::PathBuf;

use sim_rng::{Rng, Stream};

#[test]
#[ignore = "needs the golden corpus: set AR_SIM_CORPUS, run with --include-ignored"]
fn streams_match_the_reference() {
    let dir = std::env::var_os("AR_SIM_CORPUS")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../corpus"));
    let text =
        std::fs::read_to_string(dir.join("rng.jsonl")).expect("rng.jsonl: set AR_SIM_CORPUS");
    let mut streams = 0;
    let mut draws = 0;
    for line in text.lines() {
        let record: serde_json::Value = serde_json::from_str(line).unwrap();
        let run_seed = record["run_seed"].as_u64().unwrap();
        let name = record["stream"].as_str().unwrap();
        let stream = Stream::ALL
            .into_iter()
            .find(|stream| stream.name() == name)
            .unwrap();
        assert_eq!(
            stream.seed(run_seed),
            record["seed"].as_u64().unwrap(),
            "{name} seed for run {run_seed}"
        );
        let mut rng = Rng::for_stream(stream, run_seed);
        for (index, expected) in record["draws"].as_array().unwrap().iter().enumerate() {
            let expected = u64::from_str_radix(expected.as_str().unwrap(), 16).unwrap();
            assert_eq!(rng.raw(), expected, "{name} run {run_seed} draw {index}");
            draws += 1;
        }
        streams += 1;
    }
    println!("rng.jsonl: {streams} streams, {draws} raw draws identical");
    assert_eq!(streams, 27);
}
