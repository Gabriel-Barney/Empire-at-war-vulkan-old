//! Where Steam, its library folders, Forces of Corruption and the Workshop folder are, as the port's
//! `alamo_mod_check --list` reports them (ums3: the port finds them, [`crate::catalog::Install`]).

use std::path::{Path, PathBuf};

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
