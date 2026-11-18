# MEG entry lookup in retail StarWarsG.exe (mcx3 t04)

Read with REA (Ghidra 12.1.4 provider, StarWarsG.exe sha256 07daeeae…, image base 0x140000000) on 2026-11-18 for
`docs/MOD_COMPAT_DESIGN.md` §6: does the game binary-search a MEG's record table, what happens to a table that is not
sorted, to names stored with `/`, and to two records with the same CRC. Behaviour only; no code is copied.

## 1. The path from a file name to MEG data

1. `Open_File` (0x219ff0, MegaFileManagerClass) holds the manager's mutex and walks the mounted archive list. It skips
   archives flagged at +0x140 and asks each archive's lookup (0x23f880) for the name. The first archive that answers
   with an index other than -1 serves the file (the list order is the subject of `PHASE2_ANSWERS.md` §14; probe v2
   showed the later-listed MEG wins).
2. The archive lookup (0x23f880):
   - returns -1 for an empty name or an archive that is not open;
   - normalises the requested path into one string (0x23f240 splits it, the parts are joined with `\`);
   - computes CRC-32 of that string (0x215a30: reflected table at 0x140a14d20, init and final xor 0xFFFFFFFF, the
     same CRC the port's `meg::crc32` computes);
   - calls the C runtime's `bsearch` (0x785a28, the UCRT routine; Ghidra matches it to the VS2017/2019 library) over the
     archive's record table as loaded (in the reader object at archive +0x28: count at +0xC8, base at +0xD0), record size 20 bytes, the key a 20-byte
     record holding only the CRC;
   - the comparator (code at 0x240540) compares the first 32-bit field of key and record **unsigned**: 1 above, -1
     below, 0 equal;
   - on a hit it returns the found record's **second field** (the index field at +4), else -1.
3. Reading (0x23f7d0) takes that number as a record position: it opens a file object on the archive and seeks to the
   offset field (+0xC) of the record **at that position** (record base + 20 × index).

No step compares the stored file name. The name table is not used to find a file.

## 2. What follows for malformed tables

| Table defect | Retail result | Port (meg::Archive::lookup, the VFS) |
|---|---|---|
| Records not sorted by CRC | `bsearch` follows its probe order and misses records out of place; those files are not found in that MEG (a lower layer's copy, or none, is used) | Same probe order; the lost records are counted as `meg_unsorted` |
| Two or more records with one CRC | The one `bsearch` stops at first wins; which one depends on the table size and position, not file order | Same probe order; the shadowed ones are counted as `meg_duplicate` |
| Stored CRC is not the CRC of the stored name | Found by the stored CRC, i.e. under whatever name hashes to it; not found under its stored name | Same; counted as `meg_name_mismatch` |
| Stored names with `/`, lower case | No effect on lookup (names are not compared); the requested name is normalised before hashing | Same; directory listings normalise stored names (upper case, `\`) |
| Index field ≠ the record's position | The data read is the record the index names | Same; counted as `meg_index` |
| Index field ≥ record count | Reads past the record table (undefined) | [port] not found, counted as `meg_index` |

### The probe order

The UCRT `bsearch` keeps a window of `n` records starting at `lo`. While `n` > 1 it probes `lo + n/2` when `n` is odd and
`lo + n/2 − 1` when `n` is even; equal ends the search, a key below the probe keeps the records left of it (`n` becomes
the number of those), a key above keeps the `n/2` records right of it. A window of one record is compared once. The port
implements this rule in `meg::Archive::probe` and its test checks it against the C runtime's own `bsearch`
(`alamo_vfs_registry_tests`, "meg lookup").

## 3. Consequences for the port

- Design §6's question ("does retail binary-search too?") is answered yes: the port keeps the loss and reports it (P6),
  it does not build a sorted index.
- Retail's base MEGs and the six installed workshop mods (1125571106, 1976399102, 1770851727, 1397421866, 2858787558,
  3387971722) have no finding of any kind (checked with `alamo_mod_check` on 2026-11-18), so the change from the
  earlier name-confirming lookup moves no golden.
- The tools' `meg::Archive::find` (CRC then exact name) stays for the `meg` command-line tools; game reads go through
  `lookup`.
