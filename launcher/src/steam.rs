//! Finds Steam, its library folders, Forces of Corruption and the Workshop folder.

use std::path::{Path, PathBuf};

pub const EAW_APP_ID: &str = "32470";

#[derive(Clone, Debug, Default)]
pub struct SteamInfo {
    pub steam_dir: Option<PathBuf>,
    pub libraries: Vec<PathBuf>,
    /// `...\steamapps\common\Star Wars Empire at War`
    pub game_root: Option<PathBuf>,
    /// `...\Star Wars Empire at War\corruption`
    pub foc_dir: Option<PathBuf>,
    /// `...\steamapps\workshop\content\32470`
    pub workshop_dir: Option<PathBuf>,
}

/// The detection rows from the port's install locations (`alamo_mod_check --list`, [`crate::catalog::Install`]): the
/// first Steam client folder, the libraries, the game (its "corruption" folder and the folder above it) and the first
/// FoC Workshop content folder.
pub fn from_install(install: &crate::catalog::Install) -> SteamInfo {
    SteamInfo {
        steam_dir: install.steam_roots.first().cloned(),
        libraries: install.steam_libraries.clone(),
        game_root: install.foc.as_deref().and_then(Path::parent).map(Path::to_path_buf),
        foc_dir: install.foc.clone(),
        workshop_dir: install.workshop_dirs.first().cloned(),
    }
}

pub fn detect() -> SteamInfo {
    let mut info = SteamInfo { steam_dir: find_steam_dir(), ..Default::default() };
    if let Some(steam) = &info.steam_dir {
        info.libraries = library_folders(steam);
    }
    for lib in &info.libraries {
        let apps = lib.join("steamapps");
        if info.game_root.is_none() {
            let root = match install_dir_from_manifest(&apps) {
                Some(name) => apps.join("common").join(name),
                None => apps.join("common").join("Star Wars Empire at War"),
            };
            if root.is_dir() {
                let foc = root.join("corruption");
                if foc.is_dir() {
                    info.foc_dir = Some(foc);
                }
                info.game_root = Some(root);
            }
        }
        if info.workshop_dir.is_none() {
            let ws = apps.join("workshop").join("content").join(EAW_APP_ID);
            if ws.is_dir() {
                info.workshop_dir = Some(ws);
            }
        }
    }
    info
}

fn find_steam_dir() -> Option<PathBuf> {
    #[cfg(windows)]
    {
        use winreg::enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
        use winreg::RegKey;
        let candidates = [
            (HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath"),
            (HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"),
            (HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath"),
        ];
        for (hive, key, value) in candidates {
            if let Ok(k) = RegKey::predef(hive).open_subkey(key) {
                if let Ok(p) = k.get_value::<String, _>(value) {
                    let p = PathBuf::from(p.replace('/', "\\"));
                    if p.is_dir() {
                        return Some(p);
                    }
                }
            }
        }
    }
    let fallback = PathBuf::from("C:\\Program Files (x86)\\Steam");
    fallback.is_dir().then_some(fallback)
}

/// Reads `steamapps/libraryfolders.vdf`. The Steam folder itself is always a library.
fn library_folders(steam: &Path) -> Vec<PathBuf> {
    let mut out = vec![steam.to_path_buf()];
    let vdf = steam.join("steamapps").join("libraryfolders.vdf");
    if let Ok(text) = std::fs::read_to_string(vdf) {
        for line in text.lines() {
            let toks = vdf_tokens(line);
            if toks.len() == 2 && toks[0].eq_ignore_ascii_case("path") {
                let p = PathBuf::from(&toks[1]);
                if p.is_dir() && !out.iter().any(|o| same_path(o, &p)) {
                    out.push(p);
                }
            }
        }
    }
    out
}

fn install_dir_from_manifest(steamapps: &Path) -> Option<String> {
    let text = std::fs::read_to_string(steamapps.join(format!("appmanifest_{EAW_APP_ID}.acf"))).ok()?;
    text.lines().find_map(|l| {
        let t = vdf_tokens(l);
        (t.len() == 2 && t[0].eq_ignore_ascii_case("installdir")).then(|| t[1].clone())
    })
}

/// Quoted tokens on one VDF line, with `\\` unescaped.
fn vdf_tokens(line: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut chars = line.chars();
    while let Some(c) = chars.next() {
        if c != '"' {
            continue;
        }
        let mut tok = String::new();
        while let Some(c) = chars.next() {
            match c {
                '\\' => {
                    if let Some(n) = chars.next() {
                        tok.push(n);
                    }
                }
                '"' => break,
                _ => tok.push(c),
            }
        }
        out.push(tok);
    }
    out
}

pub fn same_path(a: &Path, b: &Path) -> bool {
    let norm = |p: &Path| {
        p.to_string_lossy().replace('/', "\\").trim_end_matches('\\').to_ascii_lowercase()
    };
    norm(a) == norm(b)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The settings page's detection rows from the recorded `alamo_mod_check --list` (ums3 t01).
    #[test]
    fn detection_comes_from_the_recorded_install() {
        let c = crate::catalog::parse(crate::catalog::tests::RECORDED).unwrap();
        let s = from_install(&c.install);
        assert_eq!(s.steam_dir.as_deref(), Some(Path::new("C:\\Fixture\\steamlib")));
        assert_eq!(s.libraries, vec![PathBuf::from("C:\\Fixture\\steamlib")]);
        assert_eq!(s.foc_dir.as_deref(), Some(Path::new("C:\\Fixture\\corruption")));
        assert_eq!(s.game_root.as_deref(), Some(Path::new("C:\\Fixture")));
        assert_eq!(s.workshop_dir.as_deref(), Some(Path::new("C:\\Fixture\\steamlib\\steamapps\\workshop\\content\\32470")));
        assert_eq!(from_install(&crate::catalog::Install::default()).foc_dir, None);
    }
}
