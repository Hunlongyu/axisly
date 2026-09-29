mod tree;

use std::io::{self, Read};

fn main() {
    let mut input = String::new();
    let result = io::stdin()
        .read_to_string(&mut input)
        .map_err(|error| error.to_string())
        .and_then(|_| tree::run(&input));
    if let Err(error) = result {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
