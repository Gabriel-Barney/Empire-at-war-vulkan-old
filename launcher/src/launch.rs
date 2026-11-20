//! Turns a profile into the exact argument list `alamo_game.exe` is started with.
//!
//! Facts this relies on (from the port's `game_main.cpp` / `vfs.cpp`):
//! * flags and their values are separate arguments (`--saves-dir DIR`);
//! * an unknown flag makes the game print usage and exit with code 2 (the flags it knows
//!   come from the game itself, `alamo_game.exe --list-flags --json`: [`game_flags`]); an
//!   argument with `=` in it is never a flag;
//! * `MODPATH=` may be repeated, earliest entry wins;
//! * a save only loads when the `KEY=value` arguments are byte-identical to the ones it
//!   was made with, so mod paths are always passed in the same absolute form.

use crate::config::{self, Config, Profile};
use crate::diagnostics::{self, Finding, Severity};
use crate::mods;
use crate::runner::split_args;
use crate::steam::SteamInfo;
use crate::catalog;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime};

/// `%LOCALAPPDATA%\alamo` — where the port keeps saves and video options by default.
pub fn alamo_dir() -> Option<PathBuf> {
    dirs::data_local_dir().map(|d| d.join("alamo"))
}

pub fn default_saves_dir() -> Option<PathBuf> {
    alamo_dir().map(|d| d.join("Saves"))
}

pub fn video_options_path() -> Option<PathBuf> {
    alamo_dir().map(|d| d.join("video_options.txt"))
}

/// The folder the game reads and writes saves in for this profile.
pub fn saves_dir(profile: &Profile) -> Option<PathBuf> {
    if profile.separate_saves {
        Some(config::data_dir().join("saves").join(format!("profile-{}", profile.id)))
    } else {
        default_saves_dir()
    }
}

/// `MODPATH=` arguments in priority order, plus any `KEY=value` extras. These are the
/// arguments a save remembers.
pub fn data_args(profile: &Profile) -> Vec<String> {
    let mut out: Vec<String> = profile
        .mods
        .iter()
        .map(|m| format!("MODPATH={}", m.to_string_lossy().trim_end_matches(['\\', '/'])))
        .collect();
    out.extend(split_args(&profile.options.extra_args).into_iter().filter(|a| !a.starts_with('-') && a.contains('=')));
    out
}

pub struct LaunchPlan {
    pub exe: PathBuf,
    pub work_dir: PathBuf,
    pub args: Vec<String>,
}

/// Things that would stop the game from starting, with what to do about each. These are
/// the `Error` findings of the cheap diagnostics, so the Launch button and the
/// diagnostics panel can't disagree.
pub fn blockers(cfg: &Config, steam: &SteamInfo) -> Vec<Finding> {
    let foc = mods::foc_dir(steam, &cfg.settings);
    diagnostics::quick(cfg.settings.game_exe.as_deref(), foc.as_deref(), &cfg.active().mods)
        .into_iter()
        .filter(|f| f.severity == Severity::Error)
        .collect()
}

/// Things that would stop the game from starting, in plain words.
pub fn problems(cfg: &Config, steam: &SteamInfo) -> Vec<String> {
    blockers(cfg, steam).into_iter().map(|f| f.detail).collect()
}

/// One flag `alamo_game.exe` accepts, as `alamo_game.exe --list-flags --json` reports it (schema `alamo.game_flags` v1,
/// the table `kGameFlags` that `game_main.cpp`'s argument loop dispatches through). Anything else that has no `=` in it
/// makes the game print its usage and exit with code 2.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GameFlag {
    pub flag: String,
    /// A value follows it as the next argument.
    pub value: bool,
    /// The values it accepts; empty = any.
    pub one_of: Vec<String>,
    /// The value must be a whole number above 0.
    pub positive: bool,
}

/// Reads the JSON `alamo_game.exe --list-flags --json` prints.
pub fn parse_flags(json: &str) -> Result<Vec<GameFlag>, String> {
    let v: serde_json::Value = serde_json::from_str(json).map_err(|e| format!("the flag list is not JSON: {e}"))?;
    if v.get("schema").and_then(serde_json::Value::as_str) != Some("alamo.game_flags") {
        return Err("not an alamo.game_flags list (an alamo_game.exe from before ref3?)".into());
    }
    let flags: Vec<GameFlag> = catalog::list(&v, "flags")
        .iter()
        .map(|f| GameFlag {
            flag: catalog::text(f, "flag"),
            value: catalog::flag(f, "value"),
            one_of: catalog::strings(f, "one_of"),
            positive: catalog::flag(f, "positive"),
        })
        .filter(|f| !f.flag.is_empty())
        .collect();
    if flags.is_empty() {
        return Err("the game listed no flags".into());
    }
    Ok(flags)
}

/// How long `--list-flags` may take (it prints before the game loads anything).
const FLAGS_TIMEOUT: Duration = Duration::from_secs(15);

type FlagCache = Option<(PathBuf, Option<SystemTime>, u64, Result<Arc<Vec<GameFlag>>, String>)>;
static FLAG_CACHE: Mutex<FlagCache> = Mutex::new(None);

/// The flags of the configured `alamo_game.exe`, asked from the exe itself and kept until the exe changes (path, size
/// or write time), so a rebuilt or replaced game is asked again.
pub fn game_flags(game_exe: Option<&Path>) -> Result<Arc<Vec<GameFlag>>, String> {
    let exe = game_exe.ok_or("no alamo_game.exe is set")?;
    let meta = std::fs::metadata(exe).map_err(|_| format!("{} was not found", exe.display()))?;
    let (modified, len) = (meta.modified().ok(), meta.len());
    let mut cache = FLAG_CACHE.lock().unwrap_or_else(|e| e.into_inner());
    if let Some((p, m, l, r)) = cache.as_ref() {
        if p == exe && *m == modified && *l == len {
            return r.clone();
        }
    }
    let args = ["--list-flags".to_owned(), "--json".to_owned()];
    let result = catalog::run_check(exe, &args, FLAGS_TIMEOUT).and_then(|out| parse_flags(&out)).map(Arc::new);
    *cache = Some((exe.to_path_buf(), modified, len, result.clone()));
    result
}

/// Flags with a value that the launcher passes itself. In the extra arguments they come
/// later on the command line, and the game keeps the last one it sees.
const LAUNCHER_FLAGS: [&str; 5] = ["--game", "--saves-dir", "--video-options", "--workers", "--frame-cap"];

/// Something in a profile's extra arguments the game will trip over.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ArgIssue {
    /// Not a flag the game knows, and not `KEY=value`.
    Unknown(String),
    /// The flag takes a value and none follows it.
    MissingValue(String),
    /// The flag only accepts certain values (flag, value, what it accepts).
    BadValue(String, String, String),
    /// The launcher already passes this flag; the extra one replaces it.
    Overrides(String),
    /// The game's flag list could not be read (why), so the extra arguments were not checked.
    Unchecked(String),
}

impl ArgIssue {
    pub fn title(&self) -> String {
        match self {
            ArgIssue::Unknown(a) => format!("The game doesn't know {a}"),
            ArgIssue::MissingValue(a) => format!("{a} needs a value"),
            ArgIssue::BadValue(a, _, _) => format!("{a} has a value the game rejects"),
            ArgIssue::Overrides(a) => format!("{a} replaces a launcher setting"),
            ArgIssue::Unchecked(_) => "The extra arguments weren't checked".to_owned(),
        }
    }

    pub fn text(&self) -> String {
        match self {
            ArgIssue::Unknown(a) => {
                format!("The extra argument {a} isn't one the game knows. It will print its usage and exit straight away with code 2.")
            }
            ArgIssue::MissingValue(a) => format!("The extra argument {a} takes a value, and nothing usable follows it."),
            ArgIssue::BadValue(a, v, accepts) => {
                format!("The game exits with code 2 on \"{a} {v}\": {a} accepts {accepts}.")
            }
            ArgIssue::Overrides(a) => {
                format!("The launcher already passes {a} for this profile. The one in the extra arguments comes later on the command line and wins.")
            }
            ArgIssue::Unchecked(why) => {
                format!("The launcher asks alamo_game.exe which flags it knows and couldn't ({why}), so the extra arguments weren't checked.")
            }
        }
    }
}

/// Checks a profile's extra arguments against the flags of the game it starts ([`game_flags`]); when the game can't be
/// asked, one [`ArgIssue::Unchecked`].
pub fn check_extra_args(game_exe: Option<&Path>, extra: &str) -> Vec<ArgIssue> {
    if split_args(extra).is_empty() {
        return Vec::new();
    }
    match game_flags(game_exe) {
        Ok(flags) => extra_arg_issues(extra, &flags),
        Err(why) => vec![ArgIssue::Unchecked(why)],
    }
}

/// What a flag accepts, in words, for [`ArgIssue::BadValue`]: "device, null or off".
fn accepts_text(f: &GameFlag) -> String {
    if f.positive {
        return "a whole number above 0".to_owned();
    }
    match f.one_of.split_last() {
        Some((last, rest)) if !rest.is_empty() => format!("{} or {last}", rest.join(", ")),
        Some((last, _)) => last.clone(),
        None => String::new(),
    }
}

/// Checks a profile's extra arguments the way the game's argument loop reads them.
pub fn extra_arg_issues(extra: &str, flags: &[GameFlag]) -> Vec<ArgIssue> {
    let args = split_args(extra);
    let mut out: Vec<ArgIssue> = Vec::new();
    let mut i = 0;
    while i < args.len() {
        let a = args[i].as_str();
        i += 1;
        let Some(f) = flags.iter().find(|f| f.flag == a) else {
            // Anything with an `=` is handed to the game's data layer as KEY=value.
            if !a.contains('=') {
                out.push(ArgIssue::Unknown(a.to_owned()));
            }
            continue;
        };
        if LAUNCHER_FLAGS.contains(&a) && !out.contains(&ArgIssue::Overrides(a.to_owned())) {
            out.push(ArgIssue::Overrides(a.to_owned()));
        }
        if !f.value {
            continue;
        }
        // The game takes whatever comes next as the value, even another flag.
        let value = args.get(i).map(String::as_str).filter(|v| !v.starts_with("--"));
        let Some(value) = value else {
            out.push(ArgIssue::MissingValue(a.to_owned()));
            continue;
        };
        i += 1;
        let rejected = (!f.one_of.is_empty() && !f.one_of.iter().any(|o| o == value))
            || (f.positive && value.parse::<u32>().map_or(true, |n| n == 0));
        let accepts = rejected.then(|| accepts_text(f));
        if let Some(accepts) = accepts {
            out.push(ArgIssue::BadValue(a.to_owned(), value.to_owned(), accepts));
        }
    }
    out
}

pub fn args_for(cfg: &Config, steam: &SteamInfo, profile: &Profile) -> Vec<String> {
    args_with_mods(cfg, steam, profile, &profile.mods)
}

/// Like [`args_for`] but with a different mod list (used when hunting for a crashing mod).
pub fn args_with_mods(cfg: &Config, steam: &SteamInfo, profile: &Profile, mods: &[PathBuf]) -> Vec<String> {
    let mut args = Vec::new();
    if let Some(foc) = mods::foc_dir(steam, &cfg.settings) {
        args.push("--game".to_owned());
        args.push(foc.to_string_lossy().into_owned());
    }
    let extras = split_args(&profile.options.extra_args);
    for m in mods {
        args.push(format!("MODPATH={}", m.to_string_lossy().trim_end_matches(['\\', '/'])));
    }
    // A per-profile graphics preset is its own options file, handed to the game directly.
    if let Some(preset) = profile.video_preset.as_deref().filter(|p| !p.is_empty()) {
        let path = crate::video::preset_path(preset);
        if path.is_file() {
            args.push("--video-options".to_owned());
            args.push(path.to_string_lossy().into_owned());
        }
    }
    if profile.separate_saves {
        if let Some(dir) = saves_dir(profile) {
            args.push("--saves-dir".to_owned());
            args.push(dir.to_string_lossy().into_owned());
        }
    }
    let o = &profile.options;
    if o.skip_intro {
        args.push("--no-intro".to_owned());
    }
    if o.frame_overlay {
        args.push("--frame-overlay".to_owned());
    }
    if let Some(n) = o.worker_threads {
        args.push("--workers".to_owned());
        args.push(n.to_string());
    }
    if let Some(cap) = o.frame_cap {
        args.push("--frame-cap".to_owned());
        args.push(if cap < 0 { "display".to_owned() } else { cap.to_string() });
    }
    args.extend(extras);
    args
}

#[allow(dead_code)] // the app goes through `plan_with`; kept as the plain entry point
pub fn plan(cfg: &Config, steam: &SteamInfo) -> Result<LaunchPlan, String> {
    plan_with(cfg, steam, None)
}

/// `mods_override` replaces the active profile's mod list for this one launch.
pub fn plan_with(cfg: &Config, steam: &SteamInfo, mods_override: Option<&[PathBuf]>) -> Result<LaunchPlan, String> {
    if let Some(p) = problems(cfg, steam).into_iter().next() {
        return Err(p);
    }
    let exe = cfg.settings.game_exe.clone().unwrap();
    let work_dir = exe.parent().map(|p| p.to_path_buf()).unwrap_or_else(|| PathBuf::from("."));
    let profile = cfg.active();
    if profile.separate_saves {
        if let Some(d) = saves_dir(profile) {
            std::fs::create_dir_all(&d).map_err(|e| format!("Couldn't create {}: {e}", d.display()))?;
        }
    }
    let args = match mods_override {
        Some(mods) => args_with_mods(cfg, steam, profile, mods),
        None => args_for(cfg, steam, profile),
    };
    Ok(LaunchPlan { exe, work_dir, args })
}

/// The list a current alamo_game.exe prints, for tests (the ctest alamo_game_list_flags keeps this copy equal to the
/// exe's output).
#[cfg(test)]
pub fn test_flags() -> Vec<GameFlag> {
    parse_flags(include_str!("testdata/game_flags.json")).expect("testdata/game_flags.json")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn flags() -> Vec<GameFlag> {
        test_flags()
    }

    #[test]
    fn extra_arguments_are_read_the_way_the_game_reads_them() {
        let f = flags();
        let issues = |extra: &str| extra_arg_issues(extra, &f);
        assert!(issues("").is_empty());
        assert!(issues("--attract --seed 7 AI_DEBUG=1 \"NAME=two words\" --audio null --movie Logo1").is_empty());
        // Anything with an `=` is data for the game, even when it looks like a flag.
        assert!(issues("--made-up=1").is_empty());
        assert_eq!(issues("--windowed"), vec![ArgIssue::Unknown("--windowed".into())]);
        assert_eq!(issues("fullscreen -w"), vec![ArgIssue::Unknown("fullscreen".into()), ArgIssue::Unknown("-w".into())]);
        // A flag's value is not itself checked as a flag.
        assert!(issues("--cache-dir some-folder").is_empty());
        assert_eq!(issues("--seed"), vec![ArgIssue::MissingValue("--seed".into())]);
        assert_eq!(issues("--seed --attract"), vec![ArgIssue::MissingValue("--seed".into())], "the flag after it is still read as a flag");
        assert_eq!(issues("--audio loud"), vec![ArgIssue::BadValue("--audio".into(), "loud".into(), "device, null or off".into())]);
        assert_eq!(issues("--priority high"), vec![ArgIssue::BadValue("--priority".into(), "high".into(), "above or normal".into())]);
        assert_eq!(issues("--width 0").len(), 1);
        assert!(issues("--width 1280 --fleet-groups --priority normal").is_empty(), "flags the old hand copy lacked");
        assert_eq!(issues("--frame-cap 30 --frame-cap 60 --no-intro"), vec![ArgIssue::Overrides("--frame-cap".into())]);
        assert!(ArgIssue::Unknown("--x".into()).text().contains("code 2"));
    }

    #[test]
    fn the_flag_list_comes_from_the_game() {
        assert!(parse_flags("{}").is_err());
        assert!(parse_flags("{\"schema\": \"alamo.game_flags\", \"version\": 1, \"flags\": []}").is_err());
        let f = flags();
        let audio = f.iter().find(|g| g.flag == "--audio").expect("--audio");
        assert!(audio.value);
        assert_eq!(audio.one_of, ["device", "null", "off"]);
        assert!(f.iter().any(|g| g.flag == "--hidden" && !g.value));
        // No game to ask: one note instead of a guess, and nothing for empty extras.
        assert!(check_extra_args(None, "").is_empty());
        assert!(matches!(check_extra_args(None, "--attract").as_slice(), [ArgIssue::Unchecked(_)]));
        let missing = Path::new("Z:/nowhere/alamo_game.exe");
        assert!(matches!(check_extra_args(Some(missing), "--attract").as_slice(), [ArgIssue::Unchecked(_)]));
    }

    #[test]
    fn every_flag_the_launcher_passes_is_one_the_game_knows() {
        let f = flags();
        for flag in LAUNCHER_FLAGS.iter().chain(["--no-intro", "--frame-overlay"].iter()) {
            assert!(f.iter().any(|g| g.flag == *flag), "{flag}");
        }
    }
}
