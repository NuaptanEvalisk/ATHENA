// GPL-3.0-or-later. Copyright (C) 2026 Nuaptan Felix Evalisk.
// Desktop CLI adapter for the shared Materials engine.
use std::{env, fs, process::ExitCode};

fn run() -> Result<String, String> {
    let args: Vec<String> = env::args().collect();
    let read = |path: &str| fs::read_to_string(path).map_err(|e| e.to_string());
    match args.as_slice() {
        [_, command] if command == "list-styles" => athena_materials::list_styles(),
        [_, command, path] if command == "import-bib" => athena_materials::import_bib(&read(path)?),
        [_, command, path, request] if command == "render" =>
            athena_materials::render(&read(path)?, &read(request)?),
        _ => Err("usage: athena-materials-engine list-styles | import-bib FILE | render BIB REQUEST.json".into()),
    }
}

fn main() -> ExitCode {
    match run() {
        Ok(output) => {
            println!("{output}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
