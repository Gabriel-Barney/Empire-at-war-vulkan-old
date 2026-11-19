//! "Find the crashing mod": relaunch with half the mods until one is left.
//!
//! [`Bisect`] is a plain state machine with no I/O: it proposes which mods to switch on for
//! the next run ([`Bisect::current`]), is told how that run went ([`Bisect::answer`]), and
//! ends with a [`Verdict`]. The profile's own mod list is never touched; the caller launches
//! each step with [`Bisect::enabled_paths`].
//!
//! The hunt, in order:
//! 1. **Baseline** — no mods at all. A crash here means no mod is to blame.
//! 2. **Split** — the suspects are halved until one is left. Mods already cleared stay on
//!    (the culprit may need them); mods the crash turned out not to need stay off.
//! 3. **Alone** — the last suspect by itself (plus the mods it depends on). A crash names it.
//! 4. **Together** — only if it works alone: the suspect with the cleared mods again. A crash
//!    means it only crashes in combination; no crash at all means it couldn't be reproduced.
//!
//! Runs whose result is already known from an earlier step are skipped.
//!
//! Dependencies: a submod without its parent can crash for reasons of its own, so a mod is
//! never switched on while a mod it depends on is off. They come from the mods' declared
//! dependencies as the port resolved them (catalog `dependency_roots`) first, else from the
//! Workshop ids a mod's `modinfo.json` mentions (catalog `modinfo_numbers`); see
//! [`detect_parents`]. The launcher reads no `modinfo.json` and resolves no dependency itself.

use crate::steam::same_path;
use std::collections::BTreeSet;
use std::path::PathBuf;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    Crashed,
    Worked,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TestKind {
    Baseline,
    Split,
    Alone,
    Together,
}

/// One run the hunt asks for. Mods are positions in the hunt's list.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Test {
    pub kind: TestKind,
    /// Mods switched on for this run, in load order.
    pub enabled: Vec<usize>,
    /// `(mod, parent)`: mods that are off this run because a mod they depend on is off.
    pub off_with_parent: Vec<(usize, usize)>,
    /// `(mod, parent)`: mods left on only because the mod being tested depends on them.
    pub on_for_child: Vec<(usize, usize)>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Verdict {
    /// The game crashes with this mod and nothing else (except the mods it depends on).
    Culprit { culprit: usize, with_parents: Vec<usize> },
    /// The game crashes with no mods at all.
    NoModToBlame,
    /// Nothing crashed during the hunt, not even with every mod back on.
    CantReproduce,
    /// `trigger` is the last suspect standing, but it works on its own: the crash needs it
    /// together with other mods. `set` is the smallest set of mods seen to crash.
    Combination { set: Vec<usize>, trigger: usize },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Phase {
    Baseline,
    Split,
    Alone,
    Together,
    Done,
}

#[derive(Clone, Debug)]
pub struct Bisect {
    mods: Vec<PathBuf>,
    /// `parents[i]`: the mods that mod `i` depends on.
    parents: Vec<Vec<usize>>,
    phase: Phase,
    /// Cleared: the game ran fine with these on.
    good: BTreeSet<usize>,
    /// The culprit is one of these, in load order.
    suspects: Vec<usize>,
    /// The suspects switched on in the split being run.
    half_on: Vec<usize>,
    crashed: Vec<Vec<usize>>,
    worked: Vec<Vec<usize>>,
    current: Option<Test>,
    verdict: Option<Verdict>,
    history: Vec<(Test, Outcome)>,
}

impl Bisect {
    /// `parents[i]` lists the mods (positions in `mods`) that mod `i` depends on; see
    /// [`detect_parents`]. Entries that are out of range or point at the mod itself are dropped.
    pub fn new(mods: Vec<PathBuf>, parents: Vec<Vec<usize>>) -> Self {
        let n = mods.len();
        let mut parents: Vec<Vec<usize>> = parents;
        parents.resize(n, Vec::new());
        for (i, p) in parents.iter_mut().enumerate() {
            p.retain(|&x| x < n && x != i);
            p.sort_unstable();
            p.dedup();
        }
        let mut b = Self {
            mods,
            parents,
            phase: Phase::Baseline,
            good: BTreeSet::new(),
            suspects: (0..n).collect(),
            half_on: Vec::new(),
            crashed: Vec::new(),
            worked: Vec::new(),
            current: None,
            verdict: None,
            history: Vec::new(),
        };
        b.current = Some(Test {
            kind: TestKind::Baseline,
            enabled: Vec::new(),
            off_with_parent: Vec::new(),
            on_for_child: Vec::new(),
        });
        b
    }

    /// The run to do next; `None` once there is a verdict.
    pub fn current(&self) -> Option<&Test> {
        self.current.as_ref()
    }

    pub fn verdict(&self) -> Option<&Verdict> {
        self.verdict.as_ref()
    }

    pub fn history(&self) -> &[(Test, Outcome)] {
        &self.history
    }

    /// Mods still under suspicion, in load order.
    pub fn suspects(&self) -> &[usize] {
        &self.suspects
    }

    /// 1-based number of the run being asked for (or of the last one, once finished).
    pub fn step(&self) -> usize {
        self.history.len() + usize::from(self.current.is_some())
    }

    /// Roughly how many runs the whole hunt takes: the baseline, one per halving, and the
    /// final check of the last suspect. Never less than the step it is on.
    pub fn estimated_steps(&self) -> usize {
        let n = self.mods.len().max(1);
        let halvings = (usize::BITS - (n - 1).leading_zeros()) as usize;
        (halvings + 2).max(self.step())
    }

    /// The folders to launch the current run with, in the profile's load order.
    pub fn enabled_paths(&self) -> Vec<PathBuf> {
        match &self.current {
            Some(t) => t.enabled.iter().map(|&i| self.mods[i].clone()).collect(),
            None => Vec::new(),
        }
    }

    /// Records how the current run went and works out the next one (or the verdict).
    pub fn answer(&mut self, outcome: Outcome) {
        let Some(test) = self.current.take() else { return };
        match outcome {
            Outcome::Crashed => self.crashed.push(test.enabled.clone()),
            Outcome::Worked => self.worked.push(test.enabled.clone()),
        }
        self.history.push((test, outcome));
        match (self.phase, outcome) {
            (Phase::Baseline, Outcome::Crashed) => return self.finish(Verdict::NoModToBlame),
            (Phase::Baseline, Outcome::Worked) => self.phase = Phase::Split,
            (Phase::Split, Outcome::Crashed) => {
                // The crash didn't need the half that was off: the culprit is in the half
                // that was on, and the other half stays off from here.
                self.suspects = std::mem::take(&mut self.half_on);
            }
            (Phase::Split, Outcome::Worked) => {
                let on = std::mem::take(&mut self.half_on);
                self.suspects.retain(|s| !on.contains(s));
                self.good.extend(on);
            }
            (Phase::Alone, Outcome::Crashed) => {
                let c = self.suspects[0];
                return self.finish(self.culprit(c));
            }
            (Phase::Alone, Outcome::Worked) => self.phase = Phase::Together,
            (Phase::Together, _) => return self.finish(self.combination_or_nothing()),
            (Phase::Done, _) => return,
        }
        self.advance();
    }

    fn finish(&mut self, verdict: Verdict) {
        self.phase = Phase::Done;
        self.current = None;
        self.verdict = Some(verdict);
    }

    fn culprit(&self, c: usize) -> Verdict {
        let mut with_parents: Vec<usize> = self.ancestors(c).into_iter().collect();
        with_parents.retain(|&p| p != c);
        Verdict::Culprit { culprit: c, with_parents }
    }

    /// The verdict when the last suspect works on its own.
    fn combination_or_nothing(&self) -> Verdict {
        match (self.crashed.iter().min_by_key(|s| s.len()), self.suspects.first()) {
            (Some(set), Some(&trigger)) => Verdict::Combination { set: set.clone(), trigger },
            _ => Verdict::CantReproduce,
        }
    }

    /// Picks the next run, skipping any whose result is already known.
    fn advance(&mut self) {
        loop {
            match self.phase {
                Phase::Baseline | Phase::Done => return,
                Phase::Split => {
                    if self.suspects.is_empty() {
                        return self.finish(Verdict::CantReproduce);
                    }
                    if self.suspects.len() > 1 {
                        self.current = Some(self.split());
                        return;
                    }
                    self.phase = Phase::Alone;
                }
                Phase::Alone => {
                    let c = self.suspects[0];
                    let set: Vec<usize> = self.ancestors(c).into_iter().collect();
                    if self.crashed.contains(&set) {
                        return self.finish(self.culprit(c));
                    }
                    if self.worked.contains(&set) {
                        self.phase = Phase::Together;
                        continue;
                    }
                    let on_for_child = set
                        .iter()
                        .filter(|&&p| p != c)
                        .map(|&p| (self.child_towards(p, c), p))
                        .collect();
                    self.current =
                        Some(Test { kind: TestKind::Alone, enabled: set, off_with_parent: Vec::new(), on_for_child });
                    return;
                }
                Phase::Together => {
                    let c = self.suspects[0];
                    let mut want = self.good.clone();
                    want.insert(c);
                    let set: Vec<usize> = self.closed(&want).into_iter().collect();
                    if self.crashed.contains(&set) || self.worked.contains(&set) {
                        return self.finish(self.combination_or_nothing());
                    }
                    let off_with_parent = self.dropped(&want);
                    self.current =
                        Some(Test { kind: TestKind::Together, enabled: set, off_with_parent, on_for_child: Vec::new() });
                    return;
                }
            }
        }
    }

    /// Halves the suspects: the first half (parents before the mods that need them) goes
    /// on together with everything already cleared, the rest stays off.
    fn split(&mut self) -> Test {
        let order = self.parents_first(&self.suspects);
        let half: BTreeSet<usize> = order[..order.len() / 2].iter().copied().collect();
        let mut want = self.good.clone();
        want.extend(half.iter().copied());
        let mut enabled = self.closed(&want);
        let mut on: Vec<usize> = half.iter().copied().filter(|i| enabled.contains(i)).collect();
        if on.is_empty() {
            // Only mods that depend on each other in a circle end up here. Ignore the
            // dependencies for this step rather than run a test that tells nothing.
            enabled = want.clone();
            on = half.iter().copied().collect();
        }
        // Suspects that are off and couldn't have been on anyway, plus anything cleared
        // that had to go off with them.
        let mut off_with_parent = Vec::new();
        for &d in self.suspects.iter().chain(self.good.iter()) {
            if enabled.contains(&d) {
                continue;
            }
            if let Some(&p) = self.parents[d].iter().find(|p| !enabled.contains(p)) {
                off_with_parent.push((d, p));
            }
        }
        off_with_parent.sort_unstable();
        self.half_on = on;
        Test { kind: TestKind::Split, enabled: enabled.into_iter().collect(), off_with_parent, on_for_child: Vec::new() }
    }

    /// `set` minus every mod that depends (directly or not) on a mod outside it.
    fn closed(&self, set: &BTreeSet<usize>) -> BTreeSet<usize> {
        let mut out = set.clone();
        loop {
            let drop: Vec<usize> =
                out.iter().copied().filter(|&i| self.parents[i].iter().any(|p| !out.contains(p))).collect();
            if drop.is_empty() {
                return out;
            }
            for i in drop {
                out.remove(&i);
            }
        }
    }

    /// The `(mod, missing parent)` pairs [`Self::closed`] removes from `set`.
    fn dropped(&self, set: &BTreeSet<usize>) -> Vec<(usize, usize)> {
        let kept = self.closed(set);
        set.iter()
            .filter(|i| !kept.contains(i))
            .filter_map(|&i| self.parents[i].iter().find(|p| !kept.contains(p)).map(|&p| (i, p)))
            .collect()
    }

    /// The mod and everything it depends on, directly or through other mods.
    fn ancestors(&self, i: usize) -> BTreeSet<usize> {
        let mut out = BTreeSet::from([i]);
        let mut todo = vec![i];
        while let Some(x) = todo.pop() {
            for &p in &self.parents[x] {
                if out.insert(p) {
                    todo.push(p);
                }
            }
        }
        out
    }

    /// Which of `c`'s ancestors-or-self names `p` as a parent (for "X needs Y" notes).
    fn child_towards(&self, p: usize, c: usize) -> usize {
        self.ancestors(c).into_iter().find(|&x| self.parents[x].contains(&p)).unwrap_or(c)
    }

    /// `items` reordered so a mod comes after the mods it depends on, otherwise keeping
    /// load order. A dependency circle is broken at its first mod.
    fn parents_first(&self, items: &[usize]) -> Vec<usize> {
        let mut left: Vec<usize> = items.to_vec();
        let mut out = Vec::with_capacity(left.len());
        while !left.is_empty() {
            let pos = left
                .iter()
                .position(|&i| !self.parents[i].iter().any(|p| left.contains(p)))
                .unwrap_or(0);
            out.push(left.remove(pos));
        }
        out
    }

    /// The hunt so far and its result as plain text, for the clipboard.
    pub fn summary(&self, names: &[String]) -> String {
        let name = |i: usize| names.get(i).cloned().unwrap_or_else(|| format!("mod {}", i + 1));
        let list = |set: &[usize]| {
            if set.is_empty() {
                "no mods".to_owned()
            } else {
                set.iter().map(|&i| name(i)).collect::<Vec<_>>().join(", ")
            }
        };
        let mut out = String::from("Find the crashing mod\n");
        out.push_str(&format!("Mods in the profile ({}): {}\n\n", self.mods.len(), list(&(0..self.mods.len()).collect::<Vec<_>>())));
        for (n, (test, outcome)) in self.history.iter().enumerate() {
            let result = match outcome {
                Outcome::Crashed => "crashed",
                Outcome::Worked => "worked",
            };
            out.push_str(&format!("Step {}: {} -> {}\n", n + 1, list(&test.enabled), result));
        }
        out.push('\n');
        out.push_str(&match &self.verdict {
            None => "Not finished.".to_owned(),
            Some(v) => verdict_text(v, names),
        });
        out.push('\n');
        out
    }
}

/// One or two plain sentences for a verdict.
pub fn verdict_text(v: &Verdict, names: &[String]) -> String {
    let name = |i: usize| names.get(i).cloned().unwrap_or_else(|| format!("mod {}", i + 1));
    let list = |set: &[usize]| set.iter().map(|&i| name(i)).collect::<Vec<_>>().join(", ");
    match v {
        Verdict::Culprit { culprit, with_parents } if with_parents.is_empty() => {
            format!("The crash comes from \"{}\". The game crashes with only that mod switched on.", name(*culprit))
        }
        Verdict::Culprit { culprit, with_parents } => format!(
            "The crash comes from \"{}\". The game crashes with only that mod and what it depends on ({}) switched on, and runs with {} alone.",
            name(*culprit),
            list(with_parents),
            if with_parents.len() == 1 { "that" } else { "those" }
        ),
        Verdict::NoModToBlame => "It crashes without any mods, so no mod is to blame.".to_owned(),
        Verdict::CantReproduce => {
            "Couldn't reproduce the crash: the game worked in every run, even with all the mods back on.".to_owned()
        }
        Verdict::Combination { set, trigger } => format!(
            "No single mod is to blame. \"{}\" works on its own but the game crashes when it runs together with other mods. The smallest set that crashed: {}.",
            name(*trigger),
            list(set)
        ),
    }
}

// ------------------------------------------------------------------ dependencies

/// What dependency detection needs to know about one mod.
#[derive(Clone, Debug, Default)]
pub struct ModMeta {
    /// The mod's folder.
    pub path: PathBuf,
    pub workshop_id: Option<String>,
    /// The folders of the installed mods its declared dependencies name, as the port resolved them (catalog
    /// `dependency_roots`).
    pub dependency_roots: Vec<PathBuf>,
    /// Every run of digits in the mod's `modinfo.json` (catalog `modinfo_numbers`).
    pub modinfo_numbers: Vec<String>,
}

/// For each mod, the other mods in the list it depends on. Mod A depends on listed mod B:
/// 1. first, when B's folder is one of the folders A's declared dependencies name (catalog `dependency_roots`; the
///    port resolves the dependencies, the launcher only compares folders with [`same_path`]);
/// 2. else, when none of those folders is a listed mod, when A's `modinfo.json` mentions B's Workshop id (the
///    catalog's `modinfo_numbers`). This catches string-form dependencies such as `["*","1125571106"]`, which the
///    catalog doesn't keep as declared dependencies. Only whole numbers count: a longer number that contains the id
///    doesn't.
///
/// A mod is never its own parent. Two mods that name each other (by either rule) tell nothing about which one is the
/// parent, so neither is treated as one. A mod's Workshop id is its own, else the name of a folder named with digits
/// only (a Workshop download); it needs at least 6 digits.
pub fn detect_parents(mods: &[ModMeta]) -> Vec<Vec<usize>> {
    let digits = |s: &str| s.len() >= 6 && s.bytes().all(|b| b.is_ascii_digit());
    let id = |m: &ModMeta| {
        m.workshop_id
            .as_deref()
            .map(str::trim)
            .map(str::to_owned)
            .or_else(|| m.path.file_name().map(|n| n.to_string_lossy().into_owned()))
            .filter(|w| digits(w))
    };
    let ids: Vec<Option<String>> = mods.iter().map(id).collect();
    let n = mods.len();
    let declares = |i: usize, j: usize| i != j && mods[i].dependency_roots.iter().any(|r| same_path(r, &mods[j].path));
    let mentions = |i: usize, j: usize| match &ids[j] {
        Some(id) => i != j && ids[i].as_ref() != Some(id) && mods[i].modinfo_numbers.contains(id),
        None => false,
    };
    let declared_any: Vec<bool> = (0..n).map(|i| (0..n).any(|j| declares(i, j))).collect();
    let names = |i: usize, j: usize| declares(i, j) || (!declared_any[i] && mentions(i, j));
    (0..n).map(|i| (0..n).filter(|&j| names(i, j) && !names(j, i)).collect()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn paths(n: usize) -> Vec<PathBuf> {
        (0..n).map(|i| PathBuf::from(format!("C:\\Mods\\m{i}"))).collect()
    }

    /// Plays a whole hunt against a rule for which sets of mods crash, checking on every
    /// step that the run keeps load order and never has a mod on without its parents.
    fn hunt(n: usize, parents: Vec<Vec<usize>>, crashes: impl Fn(&[usize]) -> bool) -> (Bisect, usize) {
        let acyclic = parents.iter().enumerate().all(|(i, ps)| ps.iter().all(|&p| !parents.get(p).map(|x| x.contains(&i)).unwrap_or(false)));
        let mut b = Bisect::new(paths(n), parents.clone());
        let mut steps = 0;
        let mut seen: Vec<Vec<usize>> = Vec::new();
        while let Some(t) = b.current().cloned() {
            steps += 1;
            assert!(steps <= 40, "the hunt doesn't end");
            assert_eq!(b.step(), steps);
            assert!(b.estimated_steps() >= b.step());
            assert!(t.enabled.windows(2).all(|w| w[0] < w[1]), "load order kept: {:?}", t.enabled);
            assert!(t.enabled.iter().all(|&i| i < n));
            if acyclic {
                for &i in &t.enabled {
                    for p in parents.get(i).into_iter().flatten() {
                        assert!(t.enabled.contains(p), "mod {i} is on without its parent {p}: {:?}", t.enabled);
                    }
                }
            }
            assert!(!seen.contains(&t.enabled), "the same run is asked for twice: {:?}", t.enabled);
            seen.push(t.enabled.clone());
            let expect: Vec<PathBuf> = t.enabled.iter().map(|&i| b.mods[i].clone()).collect();
            assert_eq!(b.enabled_paths(), expect);
            b.answer(if crashes(&t.enabled) { Outcome::Crashed } else { Outcome::Worked });
        }
        assert!(b.verdict().is_some());
        assert_eq!(b.history().len(), steps);
        assert!(b.enabled_paths().is_empty());
        (b, steps)
    }

    fn log2_ceil(n: usize) -> usize {
        (usize::BITS - (n.max(1) - 1).leading_zeros()) as usize
    }

    #[test]
    fn finds_a_single_culprit_wherever_it_is() {
        for n in 1..=17 {
            for culprit in 0..n {
                let (b, steps) = hunt(n, vec![], |on| on.contains(&culprit));
                assert_eq!(
                    b.verdict(),
                    Some(&Verdict::Culprit { culprit, with_parents: vec![] }),
                    "n={n} culprit={culprit}"
                );
                // Baseline, the halvings, and at most one run of the last suspect alone.
                assert!(steps <= 2 + log2_ceil(n), "n={n} culprit={culprit} took {steps}");
                assert!(steps <= b.estimated_steps());
                assert_eq!(b.suspects(), &[culprit]);
            }
        }
    }

    #[test]
    fn starts_with_no_mods_and_stops_if_that_crashes() {
        let b = Bisect::new(paths(5), vec![]);
        let first = b.current().unwrap();
        assert_eq!(first.kind, TestKind::Baseline);
        assert!(first.enabled.is_empty());
        assert_eq!(b.step(), 1);
        assert_eq!(b.estimated_steps(), 5); // baseline + 3 halvings + the last suspect alone

        let (b, steps) = hunt(5, vec![], |_| true);
        assert_eq!(steps, 1);
        assert_eq!(b.verdict(), Some(&Verdict::NoModToBlame));
        assert!(verdict_text(b.verdict().unwrap(), &[]).contains("without any mods"));
    }

    #[test]
    fn reports_when_the_crash_never_comes_back() {
        for n in [1, 2, 5, 8] {
            let (b, _) = hunt(n, vec![], |_| false);
            assert_eq!(b.verdict(), Some(&Verdict::CantReproduce), "n={n}");
            // The last run had every mod back on.
            let (last, outcome) = b.history().last().unwrap();
            assert_eq!(last.enabled, (0..n).collect::<Vec<_>>());
            assert_eq!(*outcome, Outcome::Worked);
            if n > 1 {
                assert_eq!(last.kind, TestKind::Together);
            }
        }
    }

    #[test]
    fn a_crash_that_needs_two_mods_is_reported_as_a_combination() {
        for n in 2..=9 {
            for i in 0..n {
                for j in (i + 1)..n {
                    let rule = |on: &[usize]| on.contains(&i) && on.contains(&j);
                    let (b, _) = hunt(n, vec![], rule);
                    match b.verdict().unwrap() {
                        Verdict::Combination { set, trigger } => {
                            assert!(rule(set), "n={n} {i}+{j}: reported set {set:?} doesn't crash");
                            assert!(*trigger == i || *trigger == j, "n={n} {i}+{j}: trigger {trigger}");
                            assert!(set.contains(trigger));
                            // It is the smallest crashing run of the hunt.
                            let min = b.history().iter().filter(|(_, o)| *o == Outcome::Crashed).map(|(t, _)| t.enabled.len()).min();
                            assert_eq!(Some(set.len()), min);
                        }
                        other => panic!("n={n} {i}+{j}: {other:?}"),
                    }
                }
            }
        }
    }

    #[test]
    fn a_submod_is_never_tested_without_its_parent() {
        // Mod 3 is a submod of mod 1 and crashes without it, for reasons that have nothing
        // to do with the crash being hunted. `hunt` asserts it is never on without mod 1.
        for n in [4, 6, 9] {
            for culprit in 0..n {
                let mut parents = vec![Vec::new(); n];
                parents[3] = vec![1];
                let (b, _) = hunt(n, parents, |on| on.contains(&culprit) || (on.contains(&3) && !on.contains(&1)));
                let with_parents = if culprit == 3 { vec![1] } else { vec![] };
                assert_eq!(b.verdict(), Some(&Verdict::Culprit { culprit, with_parents }), "n={n} culprit={culprit}");
            }
        }
    }

    #[test]
    fn dependents_go_off_with_their_parent_and_the_step_says_so() {
        // 1 and 2 both depend on 0; 3 depends on 2.
        let parents = vec![vec![], vec![0], vec![0], vec![2]];
        let mut b = Bisect::new(paths(4), parents);
        b.answer(Outcome::Worked); // baseline
        let t = b.current().unwrap().clone();
        assert_eq!(t.kind, TestKind::Split);
        assert_eq!(t.enabled, vec![0, 1]);
        // 3 is off and couldn't be on: its parent 2 is off.
        assert_eq!(t.off_with_parent, vec![(3, 2)]);
        b.answer(Outcome::Worked); // 0 and 1 are fine
        let t = b.current().unwrap().clone();
        assert_eq!(t.enabled, vec![0, 1, 2]);
        // Now 3 is simply the half that is off.
        assert_eq!(t.off_with_parent, vec![]);
        b.answer(Outcome::Worked); // so it is 3
        let t = b.current().unwrap().clone();
        assert_eq!(t.kind, TestKind::Alone);
        // Alone means with what it needs: 2, and 0 through 2.
        assert_eq!(t.enabled, vec![0, 2, 3]);
        assert_eq!(t.on_for_child, vec![(2, 0), (3, 2)]);
        b.answer(Outcome::Crashed);
        assert_eq!(b.verdict(), Some(&Verdict::Culprit { culprit: 3, with_parents: vec![0, 2] }));
        let names: Vec<String> = ["Base", "Addon A", "Addon B", "Patch"].iter().map(|s| s.to_string()).collect();
        let text = verdict_text(b.verdict().unwrap(), &names);
        assert!(text.contains("\"Patch\"") && text.contains("Base, Addon B"), "{text}");
    }

    #[test]
    fn a_parent_that_crashes_is_found_too() {
        let mut parents = vec![Vec::new(); 7];
        parents[5] = vec![2];
        parents[6] = vec![5];
        let (b, _) = hunt(7, parents, |on| on.contains(&2));
        assert_eq!(b.verdict(), Some(&Verdict::Culprit { culprit: 2, with_parents: vec![] }));
    }

    #[test]
    fn circular_dependencies_dont_hang_the_hunt() {
        for culprit in 0..5 {
            let parents = vec![vec![1], vec![0], vec![], vec![4], vec![3]];
            let (b, _) = hunt(5, parents, |on| on.contains(&culprit));
            assert!(
                matches!(b.verdict(), Some(Verdict::Culprit { culprit: c, .. }) if *c == culprit),
                "culprit={culprit}: {:?}",
                b.verdict()
            );
        }
        // Junk in the dependency table is ignored.
        let (b, _) = hunt(3, vec![vec![0, 9], vec![1, 1], vec![], vec![0]], |on| on.contains(&1));
        assert_eq!(b.verdict(), Some(&Verdict::Culprit { culprit: 1, with_parents: vec![] }));
    }

    #[test]
    fn known_results_are_not_run_again() {
        // Two mods, the first one crashes: the split already ran it alone.
        let (b, steps) = hunt(2, vec![], |on| on.contains(&0));
        assert_eq!(steps, 2);
        assert_eq!(b.verdict(), Some(&Verdict::Culprit { culprit: 0, with_parents: vec![] }));
        // Answers after the verdict change nothing.
        let mut b = b;
        b.answer(Outcome::Worked);
        assert_eq!(b.history().len(), 2);
        assert_eq!(b.verdict(), Some(&Verdict::Culprit { culprit: 0, with_parents: vec![] }));
    }

    #[test]
    fn an_empty_list_ends_after_the_baseline() {
        let (b, steps) = hunt(0, vec![], |_| false);
        assert_eq!((steps, b.verdict()), (1, Some(&Verdict::CantReproduce)));
    }

    #[test]
    fn the_summary_lists_every_step() {
        let (b, _) = hunt(4, vec![], |on| on.contains(&2));
        let names: Vec<String> = ["Alpha", "Bravo", "Charlie", "Delta"].iter().map(|s| s.to_string()).collect();
        let text = b.summary(&names);
        assert!(text.contains("Step 1: no mods -> worked"), "{text}");
        assert!(text.contains("Step 2: Alpha, Bravo -> worked"), "{text}");
        assert!(text.contains("Step 3: Alpha, Bravo, Charlie -> crashed"), "{text}");
        assert!(text.contains("Step 4: Charlie -> crashed"), "{text}");
        assert!(text.contains("The crash comes from \"Charlie\""), "{text}");
    }

    /// A mod in `path` with the folders its declared dependencies resolved to and the numbers its `modinfo.json`
    /// mentions.
    fn meta(path: &str, id: Option<&str>, roots: &[&str], numbers: &[&str]) -> ModMeta {
        ModMeta {
            path: PathBuf::from(path),
            workshop_id: id.map(str::to_owned),
            dependency_roots: roots.iter().map(PathBuf::from).collect(),
            modinfo_numbers: numbers.iter().map(|s| s.to_string()).collect(),
        }
    }

    /// A Workshop mod in `C:\W\<id>`.
    fn ws(id: &str, roots: &[&str], numbers: &[&str]) -> ModMeta {
        meta(&format!("C:\\W\\{id}"), Some(id), roots, numbers)
    }

    #[test]
    fn dependencies_come_from_the_resolved_dependency_folders() {
        let mods = vec![
            // 0: the parent in the game's Mods folder; names only itself.
            meta("C:\\Games\\FoC\\Mods\\Base", None, &["C:\\Games\\FoC\\Mods\\Base"], &[]),
            // 1: a submod; the port spelled the folder with '/', other case and a trailing separator.
            meta("C:\\Games\\FoC\\Mods\\Sub", None, &["c:/games/foc/MODS/base/"], &[]),
            // 2: an extra folder, and a Workshop mod that depends on it and on the submod.
            meta("D:\\Extra\\Lib", None, &[], &[]),
            ws("2222222222", &["D:\\Extra\\Lib", "C:\\Games\\FoC\\Mods\\Sub"], &[]),
            // 4: a folder that only shares a name with a listed mod is not that mod.
            meta("C:\\Games\\FoC\\Mods\\Other", None, &["E:\\Elsewhere\\Base", "C:\\Games\\FoC\\Mods"], &[]),
        ];
        assert_eq!(detect_parents(&mods), vec![vec![], vec![0], vec![], vec![1, 2], vec![]]);

        // Two mods whose dependencies name each other: no telling which is the parent.
        let mutual = vec![
            ws("1000001", &["C:\\W\\1000002"], &[]),
            ws("1000002", &["C:\\W\\1000001"], &[]),
            ws("1000003", &["C:\\W\\1000001", "C:\\W\\1000002"], &[]),
        ];
        assert_eq!(detect_parents(&mutual), vec![vec![], vec![], vec![0, 1]]);
    }

    #[test]
    fn without_a_declared_parent_mentioned_workshop_ids_count() {
        let mods = vec![
            // 0: the parent; its modinfo.json mentions only its own id.
            ws("1125571106", &[], &["1125571106"]),
            // 1: `"dependencies":["*","1125571106"]`: the catalog keeps no declared dependency, the mention counts.
            ws("2222222222", &[], &["1125571106", "2222222222"]),
            // 2: a longer number that only contains the parent's id.
            ws("3333333333", &[], &["911255711060"]),
            // 3: a short number equal to a short folder id doesn't count either.
            meta("C:\\W\\12345", None, &[], &[]),
            ws("4444444444", &[], &["12345"]),
            // 5: no id given, but the folder is a Workshop download: its name stands in for the id.
            meta("C:\\W\\5555555555", None, &[], &[]),
            meta("D:\\Extra\\Uses5", None, &[], &["5555555555"]),
        ];
        assert_eq!(detect_parents(&mods), vec![vec![], vec![0], vec![], vec![], vec![], vec![], vec![5]]);

        // Two mods mentioning each other: no telling which is the parent; a third mentioning both has two.
        let mutual = vec![ws("1000001", &[], &["1000002"]), ws("1000002", &[], &["1000001"]), ws("1000003", &[], &["1000001", "1000002"])];
        assert_eq!(detect_parents(&mutual), vec![vec![], vec![], vec![0, 1]]);
        // A declaration one way and a mention the other way is mutual too.
        let crossed = vec![ws("1000001", &["C:\\W\\1000002"], &[]), ws("1000002", &[], &["1000001"])];
        assert_eq!(detect_parents(&crossed), vec![Vec::<usize>::new(), vec![]]);
        // Ids that aren't numbers name nothing.
        let junk = vec![ws("abc123456", &[], &[]), meta("C:\\W\\b", Some(""), &[], &[]), ws("9999999", &[], &["123456", "abc123456"])];
        assert_eq!(detect_parents(&junk), vec![Vec::<usize>::new(), vec![], vec![]]);
    }

    #[test]
    fn a_declared_parent_wins_over_mentions() {
        let mods = vec![
            ws("1000001", &[], &[]),
            ws("1000002", &[], &[]),
            // Declares 0 and mentions 1 (say, in its description): only 0 is its parent.
            ws("1000003", &["C:\\W\\1000001"], &["1000001", "1000002"]),
            // Declares a mod that isn't in the list: its mentions count.
            ws("1000004", &["C:\\W\\1999999"], &["1000002", "1999999"]),
        ];
        assert_eq!(detect_parents(&mods), vec![vec![], vec![], vec![0], vec![1]]);
    }

    #[test]
    fn detected_parents_drive_the_hunt() {
        // The submod (1) declares the parent (0): the hunt never runs it without the parent.
        let mods = vec![
            ws("1125571106", &[], &[]),
            ws("2222222222", &["C:\\W\\1125571106"], &[]),
            meta("C:\\Games\\FoC\\Mods\\Plain", None, &[], &[]),
        ];
        let (b, _) = hunt(3, detect_parents(&mods), |on| on.contains(&1));
        assert_eq!(b.verdict(), Some(&Verdict::Culprit { culprit: 1, with_parents: vec![0] }));
    }
}
