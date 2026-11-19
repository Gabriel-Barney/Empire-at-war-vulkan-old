//! Mod conflict and load-order checker: the port's `alamo_mod_check --conflicts` (ums3, Stage 10; docs/MODS.md 3.1.2).
//!
//! The port decides who wins each file: it mounts the profile's mods as the game does (the earlier mod wins a loose
//! file; a later mod's loose file beats an earlier mod's MEG record; the base game's loose file beats any MEG record)
//! and reports per mod how many of its files are replaced, every pair of mods that share files with the winner, and
//! the findings: a list file (`GameObjectFiles.xml` and friends) that is never read because another layer's copy wins,
//! a submod below the mod its `modinfo.json` names, a mod with no effect, a mod without `Data`. The launcher keeps no
//! file-resolution rules of its own; it only adds how many definitions a winning XML copy takes over
//! ([`count_definitions`], through `unitdb`). Everything here runs on a worker thread ([`Checker`]); nothing touches
//! the disk on the UI thread.
//!
//! Facts this relies on (`engine/apps/game/include/alamo/game/mod_conflicts.hpp`): `--conflicts --json -` prints only
//! the JSON (`alamo.mod_conflicts`, version 1); `mods` are the mounted mods with `root`, `exists`, `has_data`, `files`,
//! `replaced_files`; `pairs` have `winner`, `loser`, `files` (normalised `DATA\...` paths), `xml_files`; `findings`
//! have `kind` (`list_ignored`, `submod_order`, `no_effect`, `no_data`), `severity`, `text`, `mods`, `order`.

use crate::steam::same_path;
use crate::unitdb::{self, BuildInput, Layer, Progress, UnitDb};
use crate::xml;
use std::collections::{HashMap, HashSet};
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::sync::Arc;

#[derive(Clone, Debug)]
pub struct ModInput {
    pub name: String,
    pub path: PathBuf,
    pub workshop_id: Option<String>,
    /// The mod scan found it. Flips when Steam finishes installing a mod, which is what
    /// makes the check run again for a list that otherwise didn't change.
    pub present: bool,
    /// The mod's version text, if it has one. A changed version means changed files.
    pub version: String,
}

/// A profile's mods in load order (first wins) and the game they sit on top of.
#[derive(Clone, Debug, Default)]
pub struct Input {
    pub mods: Vec<ModInput>,
    pub foc: Option<PathBuf>,
    /// The port's `alamo_mod_check.exe` ([`crate::catalog::mod_check_exe`]); not part of the signature.
    pub exe: Option<PathBuf>,
}

impl Input {
    /// Changes whenever the mods, their order, their names or the game folder change.
    pub fn signature(&self) -> u64 {
        let mut h = std::collections::hash_map::DefaultHasher::new();
        for m in &self.mods {
            cache_key(m).hash(&mut h);
            m.name.hash(&mut h);
            m.workshop_id.hash(&mut h);
        }
        self.foc.as_deref().map(key_of).hash(&mut h);
        h.finish()
    }
}

fn key_of(path: &Path) -> String {
    crate::config::mod_key(path)
}

/// What a mod's part of the signature is: a mod that was installed or updated since gets a new key, and with it a
/// fresh check.
fn cache_key(m: &ModInput) -> String {
    format!("{}|{}|{}", key_of(&m.path), m.present, m.version)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Severity {
    /// Worth knowing, nothing to fix.
    Info,
    Warning,
}

#[derive(Clone, Debug)]
pub struct Warning {
    pub severity: Severity,
    pub text: String,
    /// Indices into the profile's mod list.
    pub mods: Vec<usize>,
    /// Moving a mod up or down is the likely fix.
    pub order: bool,
}

/// Files two mods both have. `upper` is earlier in the list and wins all of them.
#[derive(Clone, Debug)]
pub struct PairOverlap {
    pub upper: usize,
    pub lower: usize,
    /// Paths relative to `Data`, as the upper mod spells them, sorted.
    pub shared_files: Vec<String>,
    /// How many of the shared files are `.xml`.
    pub xml_files: usize,
    /// Definitions (by name) in the lower mod's copies of shared XML data files the game
    /// loads from the upper mod, that the upper mod's copy defines too.
    pub definitions: usize,
    /// Definitions in those lower copies that the upper mod's copy doesn't have at all.
    pub lost_definitions: usize,
}

#[derive(Clone, Debug, Default)]
pub struct ModSummary {
    pub exists: bool,
    pub has_data: bool,
    /// Loose files under `Data`.
    pub files: usize,
    /// How many of them a mod higher in the list also has (and therefore wins).
    pub replaced_files: usize,
}

#[derive(Clone, Debug, Default)]
pub struct Report {
    /// Mod names in load order, as given in the input.
    pub names: Vec<String>,
    pub pairs: Vec<PairOverlap>,
    pub warnings: Vec<Warning>,
    pub per_mod: Vec<ModSummary>,
}

impl Report {
    /// Overlaps where mod `i` is the winner.
    pub fn overrides_of(&self, i: usize) -> impl Iterator<Item = &PairOverlap> {
        self.pairs.iter().filter(move |p| p.upper == i)
    }

    /// Warnings (not notes) that involve mod `i`.
    pub fn warnings_for(&self, i: usize) -> impl Iterator<Item = &Warning> {
        self.warnings.iter().filter(move |w| w.severity == Severity::Warning && w.mods.contains(&i))
    }

    /// Warnings that moving a mod would probably fix.
    pub fn order_warnings(&self) -> Vec<&Warning> {
        self.warnings.iter().filter(|w| w.severity == Severity::Warning && w.order).collect()
    }

    pub fn name(&self, i: usize) -> &str {
        self.names.get(i).map(String::as_str).unwrap_or("a mod")
    }
}

// ------------------------------------------------------------------ the port's report

/// The check's arguments for a profile's mods in load order: `alamo_mod_check --conflicts` (ums3; the port's VFS
/// decides who wins each file, MEG records included; docs/MODS.md 3.1.2).
pub fn conflict_args(foc: &Path, mods: &[PathBuf]) -> Vec<String> {
    let mut a: Vec<String> = ["--game", &foc.to_string_lossy(), "--conflicts", "--json", "-"].iter().map(|s| s.to_string()).collect();
    a.extend(mods.iter().map(|m| format!("MODPATH={}", m.to_string_lossy().trim_end_matches(['\\', '/']))));
    a
}

/// Reads the JSON `alamo_mod_check --conflicts --json -` prints (`alamo.mod_conflicts`, version 1) into the page's
/// report for `input`. The port lists the mods it mounted; each is matched to the profile's mod with the same folder,
/// so indices are the profile's, a profile mod the port did not mount keeps a row (not there, no files) and the
/// names are the launcher's. Definition counts are not in the JSON: [`count_definitions`] adds them.
pub fn parse_conflicts(json: &str, input: &Input) -> Result<Report, String> {
    use serde_json::Value;
    let v: Value = serde_json::from_str(json).map_err(|e| format!("the conflict check's report is not JSON: {e}"))?;
    if v.get("schema").and_then(Value::as_str) != Some("alamo.mod_conflicts") {
        return Err("not an alamo.mod_conflicts (an alamo_mod_check from before ums1?)".into());
    }
    let text = |x: &Value, k: &str| x.get(k).and_then(Value::as_str).unwrap_or_default().to_owned();
    let count = |x: &Value, k: &str| x.get(k).and_then(Value::as_u64).unwrap_or(0) as usize;
    let flag = |x: &Value, k: &str| x.get(k).and_then(Value::as_bool).unwrap_or(false);
    let list = |x: &Value, k: &str| x.get(k).and_then(Value::as_array).cloned().unwrap_or_default();
    let mut report = Report {
        names: input.mods.iter().map(|m| m.name.clone()).collect(),
        per_mod: vec![ModSummary::default(); input.mods.len()],
        ..Default::default()
    };
    // The port's mod index -> the profile's.
    let mut to_profile: Vec<Option<usize>> = Vec::new();
    for m in list(&v, "mods") {
        let root = PathBuf::from(text(&m, "root").replace('/', "\\"));
        let i = input.mods.iter().position(|p| same_path(&p.path, &root));
        if let Some(i) = i {
            report.per_mod[i] = ModSummary {
                exists: flag(&m, "exists"),
                has_data: flag(&m, "has_data"),
                files: count(&m, "files"),
                replaced_files: count(&m, "replaced_files"),
            };
        }
        to_profile.push(i);
    }
    let index = |x: &Value| x.as_u64().and_then(|j| to_profile.get(j as usize).copied().flatten());
    for p in list(&v, "pairs") {
        let (Some(upper), Some(lower)) = (p.get("winner").and_then(index), p.get("loser").and_then(index)) else { continue };
        report.pairs.push(PairOverlap {
            upper,
            lower,
            shared_files: list(&p, "files").iter().filter_map(|f| f.as_str().map(str::to_owned)).collect(),
            xml_files: count(&p, "xml_files"),
            definitions: 0,
            lost_definitions: 0,
        });
    }
    for f in list(&v, "findings") {
        report.warnings.push(Warning {
            severity: if text(&f, "severity") == "info" { Severity::Info } else { Severity::Warning },
            text: text(&f, "text"),
            mods: list(&f, "mods").iter().filter_map(index).collect(),
            order: flag(&f, "order"),
        });
    }
    for e in list(&v, "errors") {
        if let Some(e) = e.as_str() {
            report.warnings.push(Warning {
                severity: Severity::Warning,
                text: format!("The conflict check could not finish: {e}"),
                mods: Vec::new(),
                order: false,
            });
        }
    }
    Ok(report)
}

/// The whole check for `input`: the port's report, then the definition counts.
pub fn analyze(input: &Input) -> Report {
    let failed = |text: String| Report {
        names: input.mods.iter().map(|m| m.name.clone()).collect(),
        per_mod: vec![ModSummary::default(); input.mods.len()],
        warnings: vec![Warning { severity: Severity::Warning, text, mods: Vec::new(), order: false }],
        ..Default::default()
    };
    let Some(foc) = input.foc.as_deref() else {
        return failed("The conflict check needs the Forces of Corruption folder (Settings, Game).".into());
    };
    let Some(exe) = input.exe.as_deref() else {
        return failed("The conflict check needs alamo_mod_check.exe next to the game.".into());
    };
    let paths: Vec<PathBuf> = input.mods.iter().map(|m| m.path.clone()).collect();
    let mut report = match crate::catalog::run_check(exe, &conflict_args(foc, &paths)).and_then(|out| parse_conflicts(&out, input)) {
        Ok(r) => r,
        Err(e) => return failed(format!("The conflict check failed: {e}")),
    };
    count_definitions(input, &mut report);
    report
}

fn read_text(path: &Path) -> Option<String> {
    std::fs::read(path).ok().map(|bytes| xml::decode(&bytes).0)
}

/// For XML data files the game really loads from a mod, counts what that copy takes over from the same-named file in
/// each mod it wins against (loose copies; a file only in a MEG counts nothing here).
fn count_definitions(input: &Input, report: &mut Report) {
    if !report.pairs.iter().any(|p| p.xml_files > 0) {
        return;
    }
    // The index resolves the list files and every file they name.
    let foc = input.foc.clone().unwrap_or_else(|| std::env::temp_dir().join("command-center-no-game-folder"));
    let db = UnitDb::build(
        BuildInput { mods: input.mods.iter().map(|m| (m.name.clone(), m.path.clone())).collect(), foc },
        &Progress::default(),
    );
    // (mod, key relative to Data, lower case) of each XML data file loaded from a mod. A file can be named by two lists.
    let mut loaded: Vec<(usize, String)> = Vec::new();
    for f in &db.files {
        let Layer::Mod(m) = &f.layer else { continue };
        let m = *m;
        let rel = f.rel.replace('/', "\\").to_lowercase();
        let Some(key) = rel.strip_prefix("data\\") else { continue };
        if !loaded.iter().any(|(lm, lk)| *lm == m && lk == key) {
            loaded.push((m, key.to_owned()));
        }
    }
    drop(db);

    let mut names: HashMap<(usize, String), Arc<HashSet<String>>> = HashMap::new();
    let mut names_of = |m: usize, key: &str| -> Arc<HashSet<String>> {
        names
            .entry((m, key.to_owned()))
            .or_insert_with(|| {
                let text = read_text(&input.mods[m].path.join("Data").join(key)).unwrap_or_default();
                Arc::new(unitdb::definition_names(&text).into_iter().map(|n| n.to_lowercase()).collect())
            })
            .clone()
    };
    for pair in report.pairs.iter_mut().filter(|p| p.xml_files > 0) {
        // The pair's shared files as Data-relative lower-case keys ("DATA\XML\UNITS.XML" -> "xml\units.xml").
        let shared: HashSet<String> = pair
            .shared_files
            .iter()
            .map(|f| f.replace('/', "\\").to_lowercase())
            .map(|f| f.strip_prefix("data\\").map(str::to_owned).unwrap_or(f))
            .collect();
        for (m, key) in &loaded {
            if *m != pair.upper || !shared.contains(key) {
                continue;
            }
            let upper = names_of(pair.upper, key);
            let lower = names_of(pair.lower, key);
            let taken = lower.iter().filter(|n| upper.contains(*n)).count();
            pair.definitions += taken;
            pair.lost_definitions += lower.len() - taken;
        }
    }
}

// ------------------------------------------------------------------ worker

struct Finished {
    signature: u64,
    report: Report,
}

/// Runs [`analyze`] on a worker thread and keeps the last result per mod-list signature.
#[derive(Default)]
pub struct Checker {
    done: Option<(u64, Arc<Report>)>,
    running: Option<(u64, Receiver<Finished>)>,
    /// Set when the worker thread couldn't be started or died.
    pub error: Option<String>,
}

impl Checker {
    /// Call once per frame with the current profile. Returns the report for exactly this
    /// input once it is ready; until then a check is running in the background. With
    /// `start` false a missing report is not worked out now (the game is running and
    /// shouldn't have to share the disk with a walk through every mod folder).
    pub fn poll(&mut self, input: &Input, ctx: &egui::Context, start: bool) -> Option<Arc<Report>> {
        let signature = input.signature();
        if let Some((sig, rx)) = &self.running {
            match rx.try_recv() {
                Ok(finished) => {
                    self.done = Some((finished.signature, Arc::new(finished.report)));
                    self.running = None;
                }
                Err(TryRecvError::Empty) => {}
                Err(TryRecvError::Disconnected) => {
                    self.error = Some("The conflict check stopped unexpectedly.".to_owned());
                    // Don't try the same list again every frame.
                    self.done = Some((*sig, Arc::new(Report::default())));
                    self.running = None;
                }
            }
        }
        if let Some((sig, report)) = &self.done {
            if *sig == signature {
                return Some(report.clone());
            }
        }
        if self.running.is_none() && start {
            if input.mods.is_empty() {
                self.done = Some((signature, Arc::new(Report::default())));
                return self.done.as_ref().map(|(_, r)| r.clone());
            }
            self.start(input.clone(), signature, ctx);
        }
        None
    }

    fn start(&mut self, input: Input, signature: u64, ctx: &egui::Context) {
        self.error = None;
        let (tx, rx) = mpsc::channel();
        let ctx = ctx.clone();
        let spawned = std::thread::Builder::new().name("conflict-check".into()).spawn(move || {
            let report = analyze(&input);
            let _ = tx.send(Finished { signature, report });
            ctx.request_repaint();
        });
        match spawned {
            Ok(_) => self.running = Some((signature, rx)),
            Err(e) => {
                self.error = Some(format!("The conflict check couldn't start: {e}"));
                self.done = Some((signature, Arc::new(Report::default())));
            }
        }
    }

    /// Forgets the last report, so the next [`Checker::poll`] asks the port again.
    pub fn refresh(&mut self) {
        self.done = None;
        self.error = None;
    }
}

// ------------------------------------------------------------------ tests

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    /// `alamo_mod_check --conflicts` on the port's conformance install for the load order Stack Parent, Stack Sub,
    /// OtherTc, STEAMMOD=900000201 (its first variant), recorded 2026-11-18 with the folder renamed to C:/Fixture.
    const RECORDED: &str = include_str!("testdata/mod_conflicts.json");

    fn recorded_input(extra_first: Option<&str>) -> Input {
        let fixture = |rel: &str| PathBuf::from(format!("C:\\Fixture\\{rel}"));
        let mut mods: Vec<ModInput> = [
            ("Parent (mine)", "corruption\\Mods\\StackParent", None),
            ("Sub (mine)", "corruption\\Mods\\StackSub", None),
            ("Other (mine)", "corruption\\Mods\\OtherTc", None),
            ("Patch (mine)", "steamlib\\steamapps\\workshop\\content\\32470\\900000201", Some("900000201")),
        ]
        .iter()
        .map(|(name, rel, id)| ModInput {
            name: (*name).into(),
            path: fixture(rel),
            workshop_id: id.map(str::to_owned),
            present: true,
            version: String::new(),
        })
        .collect();
        if let Some(missing) = extra_first {
            mods.insert(0, ModInput { name: missing.into(), path: fixture(missing), workshop_id: None, present: false, version: String::new() });
        }
        Input { mods, foc: Some(PathBuf::from("C:\\Fixture\\corruption")), exe: None }
    }

    /// The conflicts page from the recorded report (ums3 t01): who wins, how much of each mod is replaced, the
    /// warnings in the page's order, warnings before notes.
    #[test]
    fn the_conflicts_page_comes_from_the_recorded_report() {
        let r = parse_conflicts(RECORDED, &recorded_input(None)).unwrap();
        assert_eq!(r.names, ["Parent (mine)", "Sub (mine)", "Other (mine)", "Patch (mine)"]);
        assert_eq!(r.per_mod.iter().map(|m| (m.files, m.replaced_files)).collect::<Vec<_>>(), [(5, 0), (2, 1), (2, 2), (1, 0)]);
        assert!(r.per_mod.iter().all(|m| m.exists && m.has_data));
        assert_eq!(r.pairs.len(), 2);
        assert_eq!((r.pairs[0].upper, r.pairs[0].lower, r.pairs[0].xml_files), (0, 1, 1));
        assert_eq!(r.pairs[0].shared_files, ["DATA\\XML\\GAMEOBJECTFILES.XML"]);
        assert_eq!(r.overrides_of(0).count(), 2);
        // The port's order: list files lost, submods below their parents, mods with no effect; notes last.
        let shown: Vec<(Severity, bool)> = r.warnings.iter().map(|w| (w.severity, w.order)).collect();
        assert_eq!(
            shown,
            [(Severity::Warning, true), (Severity::Warning, true), (Severity::Warning, true), (Severity::Warning, true), (Severity::Info, false)]
        );
        assert!(r.warnings[1].text.contains("is a submod of") && r.warnings[3].text.contains("has no effect"));
        assert_eq!(r.warnings[0].mods, [1, 0]);
        assert!(r.warnings[0].text.contains("SubObjects.xml") && r.warnings[0].order);
        assert_eq!(r.warnings_for(2).count(), 1);
        assert_eq!(r.order_warnings().len(), 4);
        assert!(parse_conflicts("{}", &recorded_input(None)).is_err() && parse_conflicts("not json", &recorded_input(None)).is_err());
    }

    /// A profile mod the port did not mount keeps its row; the port's indices move to the profile's.
    #[test]
    fn the_report_follows_the_profile_order() {
        let r = parse_conflicts(RECORDED, &recorded_input(Some("Gone"))).unwrap();
        assert_eq!(r.names[0], "Gone");
        assert!(!r.per_mod[0].exists && r.per_mod[0].files == 0);
        assert_eq!((r.pairs[0].upper, r.pairs[0].lower), (1, 2));
        assert_eq!(r.warnings[0].mods, [2, 1]);
    }

    #[test]
    fn the_conflict_check_gets_the_mods_in_load_order() {
        let a = conflict_args(Path::new("C:\\FoC"), &[PathBuf::from("C:\\m\\Sub\\"), PathBuf::from("C:\\m\\Parent")]);
        assert_eq!(a, ["--game", "C:\\FoC", "--conflicts", "--json", "-", "MODPATH=C:\\m\\Sub", "MODPATH=C:\\m\\Parent"]);
    }

    #[test]
    fn without_the_game_or_the_check_the_page_says_why() {
        let mut input = recorded_input(None);
        input.foc = None;
        let r = analyze(&input);
        assert_eq!(r.per_mod.len(), 4);
        assert!(r.warnings.len() == 1 && r.warnings[0].text.contains("Forces of Corruption"));
        let r = analyze(&recorded_input(None));
        assert!(r.warnings[0].text.contains("alamo_mod_check.exe"));
    }

    static NEXT: AtomicUsize = AtomicUsize::new(0);

    /// A scratch folder that is removed when the test ends.
    struct Scratch(PathBuf);

    impl Scratch {
        fn new() -> Self {
            let dir = std::env::temp_dir().join(format!(
                "cc-conflicts-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::SeqCst)
            ));
            let _ = std::fs::remove_dir_all(&dir);
            std::fs::create_dir_all(&dir).unwrap();
            Scratch(dir)
        }

        fn write(&self, rel: &str, text: &str) {
            let path = self.0.join(rel);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, text).unwrap();
        }

        fn input(&self, mods: &[&str]) -> Input {
            std::fs::create_dir_all(self.0.join("game").join("corruption")).unwrap();
            Input {
                mods: mods
                    .iter()
                    .map(|name| ModInput {
                        name: (*name).to_owned(),
                        path: self.0.join(name),
                        workshop_id: None,
                        present: true,
                        version: String::new(),
                    })
                    .collect(),
                foc: Some(self.0.join("game").join("corruption")),
                exe: None,
            }
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn list(files: &[&str]) -> String {
        let body: String = files.iter().map(|f| format!("  <File>{f}</File>\n")).collect();
        format!("<?xml version=\"1.0\"?>\n<Game_Object_Files>\n{body}</Game_Object_Files>\n")
    }

    fn units(names: &[&str]) -> String {
        let body: String = names.iter().map(|n| format!("  <SpaceUnit Name=\"{n}\"><Health>10</Health></SpaceUnit>\n")).collect();
        format!("<?xml version=\"1.0\"?>\n<SpaceUnits>\n{body}</SpaceUnits>\n")
    }

    /// A report with one pair per (upper, lower, shared files), as the port gives it.
    fn report(input: &Input, pairs: &[(usize, usize, &[&str])]) -> Report {
        Report {
            names: input.mods.iter().map(|m| m.name.clone()).collect(),
            per_mod: vec![ModSummary::default(); input.mods.len()],
            pairs: pairs
                .iter()
                .map(|(u, l, files)| PairOverlap {
                    upper: *u,
                    lower: *l,
                    shared_files: files.iter().map(|f| f.to_string()).collect(),
                    xml_files: files.iter().filter(|f| f.ends_with(".XML")).count(),
                    definitions: 0,
                    lost_definitions: 0,
                })
                .collect(),
            ..Default::default()
        }
    }

    #[test]
    fn definitions_taken_over_are_counted_for_files_that_load() {
        let s = Scratch::new();
        s.write("A/Data/XML/GameObjectFiles.xml", &list(&["Units.xml", "Extra.xml"]));
        s.write("A/Data/XML/Units.xml", &units(&["One", "TWO", "Three"]));
        s.write("B/Data/XML/Units.xml", &units(&["one", "Two", "Four", "Five"]));
        // Shared, but no list names it: the game never opens it, so nothing is counted.
        s.write("A/Data/XML/Unlisted.xml", &units(&["Q"]));
        s.write("B/Data/XML/Unlisted.xml", &units(&["Q"]));
        let input = s.input(&["A", "B"]);
        let mut r = report(&input, &[(0, 1, &["DATA\\XML\\UNITS.XML", "DATA\\XML\\UNLISTED.XML"])]);
        count_definitions(&input, &mut r);
        assert_eq!((r.pairs[0].definitions, r.pairs[0].lost_definitions), (2, 2));
    }

    #[test]
    fn definitions_belong_to_the_mod_the_file_is_loaded_from() {
        let s = Scratch::new();
        s.write("Top/Data/XML/GameObjectFiles.xml", &list(&["Units.xml"]));
        s.write("Top/Data/XML/Units.xml", &units(&["One"]));
        s.write("Mid/Data/XML/Units.xml", &units(&["One", "Two"]));
        s.write("Low/Data/XML/Units.xml", &units(&["One", "Two"]));
        let input = s.input(&["Top", "Mid", "Low"]);
        let u: &[&str] = &["DATA\\XML\\UNITS.XML"];
        let mut r = report(&input, &[(0, 1, u), (0, 2, u), (1, 2, u)]);
        count_definitions(&input, &mut r);
        let pair = |a: usize, b: usize| r.pairs.iter().find(|p| p.upper == a && p.lower == b).unwrap();
        assert_eq!((pair(0, 1).definitions, pair(0, 1).lost_definitions), (1, 1));
        assert_eq!((pair(0, 2).definitions, pair(0, 2).lost_definitions), (1, 1));
        // Mid's copy is never loaded, so it takes nothing over from Low.
        assert_eq!((pair(1, 2).definitions, pair(1, 2).lost_definitions), (0, 0));
    }

    #[test]
    fn the_signature_follows_order() {
        let s = Scratch::new();
        let ab = s.input(&["A", "B"]);
        let ba = s.input(&["B", "A"]);
        assert_ne!(ab.signature(), ba.signature());
        assert_eq!(ab.signature(), s.input(&["A", "B"]).signature());
        // A mod that was updated or (re)installed is checked again.
        let mut updated = ba.clone();
        updated.mods[0].version = "2.0".into();
        assert_ne!(updated.signature(), ba.signature());
    }

    /// The real check on this PC's installed Workshop mods (the port's alamo_mod_check next to the launcher or the
    /// configured game). `cargo test live_workshop_conflicts -- --ignored --nocapture`
    #[test]
    #[ignore = "runs alamo_mod_check on the installed Workshop mods"]
    fn live_workshop_conflicts() {
        let Ok(catalog) = crate::catalog::load_for(&crate::config::Settings::default()) else {
            println!("no alamo_mod_check or no game on this PC");
            return;
        };
        let mods: Vec<ModInput> = crate::mods::from_catalog(&catalog)
            .iter()
            .filter(|m| m.workshop_id.is_some())
            .map(|m| ModInput { name: m.name.clone(), path: m.path.clone(), workshop_id: m.workshop_id.clone(), present: true, version: m.version.clone() })
            .collect();
        let input = Input { mods, foc: catalog.install.foc.clone(), exe: crate::catalog::mod_check_exe(None) };
        let start = std::time::Instant::now();
        let report = analyze(&input);
        println!("check: {:?}", start.elapsed());
        for (name, m) in report.names.iter().zip(&report.per_mod) {
            println!("  {name}: {} files, {} replaced, data={}", m.files, m.replaced_files, m.has_data);
        }
        for w in &report.warnings {
            println!("  {:?} order={}: {}", w.severity, w.order, w.text);
        }
    }
}
