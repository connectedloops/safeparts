use assert_cmd::Command;
use predicates::prelude::*;
use safeparts_desktop_bridge::{ShareEncoding, Status, new_operation};

fn run_split(encoding: &str, k: u8, n: u8, input: &[u8], passphrase: Option<&str>) -> Vec<String> {
    let mut cmd = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));

    cmd.args([
        "split",
        "-k",
        &k.to_string(),
        "-n",
        &n.to_string(),
        "-e",
        encoding,
    ]);

    if let Some(passphrase) = passphrase {
        cmd.args(["-p", passphrase]);
    }

    let assert = cmd.write_stdin(input).assert().success();

    let stdout = String::from_utf8(assert.get_output().stdout.clone()).unwrap();
    stdout
        .lines()
        .map(str::trim)
        .filter(|l| !l.is_empty())
        .map(str::to_string)
        .collect()
}

#[test]
fn version_matches_package_release() {
    Command::new(assert_cmd::cargo::cargo_bin!("safeparts"))
        .arg("--version")
        .assert()
        .success()
        .stdout(predicate::str::contains(env!("CARGO_PKG_VERSION")));
}

fn run_combine(encoding: Option<&str>, shares: &[String], passphrase: Option<&str>) -> Vec<u8> {
    let stdin = shares.join("\n") + "\n";

    let mut cmd = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));
    cmd.arg("combine");

    if let Some(encoding) = encoding {
        cmd.args(["-e", encoding]);
    }

    if let Some(passphrase) = passphrase {
        cmd.args(["-p", passphrase]);
    }

    let assert = cmd.write_stdin(stdin).assert().success();
    assert.get_output().stdout.clone()
}

#[test]
fn desktop_and_cli_interoperate_in_both_directions() {
    let desktop_secret = b"desktop output consumed by CLI";
    let mut desktop = new_operation();
    assert!(
        desktop
            .create(1, desktop_secret, 2, 3, ShareEncoding::Base58check)
            .status
            == Status::Ok
    );
    let desktop_shares = [0, 2]
        .into_iter()
        .map(|index| {
            String::from_utf8(desktop.encode_share(1, index).bytes)
                .unwrap_or_else(|error| panic!("desktop share was not UTF-8: {error}"))
        })
        .collect::<Vec<_>>();
    assert_eq!(run_combine(None, &desktop_shares, None), desktop_secret);

    let cli_secret = b"CLI output consumed by desktop";
    let cli_shares = run_split("mnemo-bip39", 2, 3, cli_secret, None);
    let mut recovery = new_operation();
    assert!(
        recovery
            .add_recovery(2, cli_shares[2].as_bytes(), ShareEncoding::Auto)
            .status
            == Status::NotEnoughShares
    );
    let ready = recovery.add_recovery(2, cli_shares[0].as_bytes(), ShareEncoding::Auto);
    assert!(ready.status == Status::Ok);
    assert!(ready.ready);
    assert_eq!(recovery.recover(2).bytes, cli_secret);
}

#[test]
fn explicit_dash_paths_use_stdin_and_stdout() {
    let input = b"explicit stdio paths";
    let mut split = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));
    let split_output = split
        .args([
            "split", "-k", "2", "-n", "3", "-e", "base64", "-i", "-", "-o", "-",
        ])
        .write_stdin(input.as_slice())
        .assert()
        .success()
        .get_output()
        .stdout
        .clone();
    let shares = String::from_utf8(split_output).unwrap();

    let mut combine = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));
    combine
        .args(["combine", "-i", "-", "-o", "-"])
        .write_stdin(shares)
        .assert()
        .success()
        .stdout(input.as_slice());
}

#[test]
fn e2e_round_trip_base58() {
    let input = b"hello e2e base58";
    let shares = run_split("base58", 2, 3, input, None);
    let recovered = run_combine(None, &shares[..2], None);
    assert_eq!(recovered, input);
}

#[test]
fn e2e_round_trip_base64() {
    let input = b"hello e2e base64";
    let shares = run_split("base64", 2, 3, input, None);
    let recovered = run_combine(None, &shares[..2], None);
    assert_eq!(recovered, input);
}

#[test]
fn auto_encoding_accepts_same_line_compact_shares() {
    for encoding in ["base64url", "base58check"] {
        let input = format!("synthetic same-line {encoding}");
        let shares = run_split(encoding, 2, 3, input.as_bytes(), None);
        let stdin = format!("{} \t {}", shares[0], shares[1]);
        let mut command = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));

        command
            .arg("combine")
            .write_stdin(stdin)
            .assert()
            .success()
            .stdout(input);
    }
}

#[test]
fn e2e_round_trip_mnemo_words() {
    let input = b"hello e2e mnemo words";
    let shares = run_split("mnemo-words", 2, 3, input, None);
    let recovered = run_combine(None, &shares[..2], None);
    assert_eq!(recovered, input);
}

#[test]
fn e2e_round_trip_mnemo_bip39() {
    let input = b"hello e2e mnemo bip39";
    let shares = run_split("mnemo-bip39", 2, 3, input, None);
    let recovered = run_combine(None, &shares[..2], None);
    assert_eq!(recovered, input);
}

#[test]
fn e2e_round_trip_encrypted_base64() {
    let input = b"hello e2e encrypted";
    let shares = run_split("base64", 2, 3, input, Some("passphrase"));
    let recovered = run_combine(None, &shares[..2], Some("passphrase"));
    assert_eq!(recovered, input);
}

#[test]
fn encrypted_without_passphrase_fails() {
    let input = b"hello";
    let shares = run_split("base64", 2, 3, input, Some("pw"));

    let stdin = format!("{}\n{}\n", shares[0], shares[1]);
    let mut cmd = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));
    cmd.args(["combine"])
        .write_stdin(stdin)
        .assert()
        .failure()
        .stderr(predicate::str::contains("passphrase required"));
}

#[test]
fn wrong_passphrase_fails() {
    let input = b"hello";
    let shares = run_split("base64", 2, 3, input, Some("pw"));

    let stdin = format!("{}\n{}\n", shares[0], shares[1]);
    let mut cmd = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));
    cmd.args(["combine", "-p", "wrong"])
        .write_stdin(stdin)
        .assert()
        .failure();
}

#[test]
fn combine_with_insufficient_shares_fails() {
    let input = b"insufficient";
    let shares = run_split("base64", 2, 3, input, None);

    let stdin = format!("{}\n", shares[0]);
    let mut cmd = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));
    cmd.args(["combine"])
        .write_stdin(stdin)
        .assert()
        .failure()
        .stderr(predicate::str::contains("need at least k shares"));
}

#[test]
fn malformed_share_errors_do_not_echo_input() {
    let sensitive = "SECRET-SHARE-TEXT";
    let mut command = Command::new(assert_cmd::cargo::cargo_bin!("safeparts"));

    command
        .args(["combine", "-e", "mnemo-words"])
        .write_stdin(sensitive)
        .assert()
        .failure()
        .stderr(predicate::str::contains(sensitive).not())
        .stderr(predicate::str::contains("could not be decoded"));
}

#[cfg(unix)]
#[test]
fn sensitive_output_files_are_owner_only_even_when_overwritten() {
    use std::fs;
    use std::os::unix::fs::PermissionsExt;

    let dir = tempfile::tempdir().unwrap();
    let shares_path = dir.path().join("shares.txt");
    let secret_path = dir.path().join("secret.bin");
    for path in [&shares_path, &secret_path] {
        fs::write(path, b"old").unwrap();
        fs::set_permissions(path, fs::Permissions::from_mode(0o644)).unwrap();
    }

    Command::new(assert_cmd::cargo::cargo_bin!("safeparts"))
        .args([
            "split",
            "-k",
            "1",
            "-n",
            "1",
            "-e",
            "base64url",
            "-o",
            shares_path.to_str().unwrap(),
        ])
        .write_stdin(b"synthetic private output")
        .assert()
        .success();
    assert_eq!(
        fs::metadata(&shares_path).unwrap().permissions().mode() & 0o077,
        0
    );

    Command::new(assert_cmd::cargo::cargo_bin!("safeparts"))
        .args([
            "combine",
            "-e",
            "base64url",
            "-i",
            shares_path.to_str().unwrap(),
            "-o",
            secret_path.to_str().unwrap(),
        ])
        .assert()
        .success();
    assert_eq!(
        fs::metadata(&secret_path).unwrap().permissions().mode() & 0o077,
        0
    );
    assert_eq!(fs::read(secret_path).unwrap(), b"synthetic private output");
}
