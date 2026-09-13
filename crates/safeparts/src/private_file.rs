use std::fs;
use std::io::Write;
use std::path::Path;

use anyhow::{Context, Result, bail};
use tempfile::NamedTempFile;

fn path_contains_nul(path: &Path) -> bool {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        path.as_os_str().as_bytes().contains(&0)
    }
    #[cfg(windows)]
    {
        use std::os::windows::ffi::OsStrExt;
        path.as_os_str().encode_wide().any(|unit| unit == 0)
    }
    #[cfg(not(any(unix, windows)))]
    {
        path.as_os_str().to_string_lossy().contains('\0')
    }
}

pub fn write(path: &Path, bytes: &[u8]) -> Result<()> {
    if path_contains_nul(path) {
        bail!("output path contains a NUL byte");
    }

    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let mut temporary = NamedTempFile::new_in(parent)
        .with_context(|| format!("create temporary output in {}", parent.display()))?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(temporary.path(), fs::Permissions::from_mode(0o600))
            .context("set private output permissions")?;
    }

    temporary
        .write_all(bytes)
        .with_context(|| format!("write temporary output for {}", path.display()))?;
    temporary
        .as_file_mut()
        .sync_all()
        .with_context(|| format!("sync temporary output for {}", path.display()))?;
    temporary
        .persist(path)
        .map_err(|error| error.error)
        .with_context(|| format!("replace output {}", path.display()))?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn failed_replace_removes_temporary_output() {
        let parent = tempfile::tempdir().unwrap();
        let destination = parent.path().join("existing-directory");
        fs::create_dir(&destination).unwrap();

        assert!(write(&destination, b"synthetic sensitive output").is_err());
        assert_eq!(fs::read_dir(parent.path()).unwrap().count(), 1);
    }

    #[test]
    fn nul_destination_is_rejected_without_prefix_clobber_or_temporary_output() {
        let parent = tempfile::tempdir().unwrap();
        let prefix = parent.path().join("synthetic-sensitive");
        fs::write(&prefix, b"keep original").unwrap();
        let destination = parent.path().join("synthetic-sensitive\0path");

        let error = write(&destination, b"replacement").unwrap_err();

        assert!(error.to_string().contains("NUL byte"));
        assert_eq!(fs::read(&prefix).unwrap(), b"keep original");
        assert_eq!(fs::read_dir(parent.path()).unwrap().count(), 1);
    }
}
