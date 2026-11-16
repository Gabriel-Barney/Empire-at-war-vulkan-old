#!/usr/bin/env python3
"""build_q6t33_mod.py - builds the Q6 t33 / t35 probe mod (Mods\\Q6T33Probe) from the installed game.

  build_q6t33_mod.py [--game DIR] [--out MODDIR] [--source-zip extracted\\foc_data_text.zip] [--no-text]

Nothing from the game is stored in the repository: CampaignFiles.xml and the English text file are read from the
install (tools/probe_mod/common/ted_owners.py) and patched. The galaxy, the campaign template and the plot files are
the q6 t15 kit's (../q6_t15_probes/build_q6_mod.py), so these runs are comparable with the q6 t25 runs of
2026-11-09 (ELIM D and ELIM E).

Mods\\Q6T33Probe:
  Data\\XML\\CampaignFiles.xml                   shipped list + Q6T33_Campaigns.xml
  Data\\XML\\Q6T33_Campaigns.xml                 one campaign per VARIANTS entry ("Q6T33: <variant>", human Empire)
  Data\\XML\\Q6_Story_Plots_<key>.xml, Q6_Story_<key>.xml   plot + story (STORY_ELAPSED 1 starts the thread)
  Data\\Scripts\\Story\\Q6T33_Story_<variant>.lua  q6t33_story.lua with VARIANT / MODE / MSG filled in
  Data\\Text\\MasterTextFile_English.dat          shipped text + TEXTS
  Q6T33_PROBE_MARKER.txt
"""
import argparse
import os
import shutil
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'q6_t15_probes'))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'common'))
sys.path.insert(0, os.path.dirname(HERE))
import build_q6_mod as q6  # noqa: E402
import ted_owners  # noqa: E402
from build_probe import dat_read, dat_bytes  # noqa: E402

MOD_NAME = 'Q6T33Probe'
MARKER = 'Q6T33_PROBE_MARKER.txt'

# The Game_Message texts, as in the q6 t25 runs: E_TAKEN (81 characters), D_GIVEN (58); LONG has 150.
MSG_E_TAKEN = 'Q6 PROBE ELIM E: the script made Tatooine neutral. You own a fleet and no planet.'
MSG_D_GIVEN = 'Q6 PROBE ELIM D: the script gave Abregado Rae to the Rebel'
MSG_LONG = ('Q6T33 LONG MESSAGE: this advisor text is 150 characters long so that it stays up for about 15 seconds '
            'by the hint rule, padded with dots .............')
assert len(MSG_E_TAKEN) == 81 and len(MSG_D_GIVEN) == 58 and len(MSG_LONG) == 150, (
    len(MSG_E_TAKEN), len(MSG_D_GIVEN), len(MSG_LONG))

# variant -> (mode, message text id or '', Empire starting forces, cursor during the run, what it separates)
VARIANTS = {
    'E_BASE': ('E', 'TEXT_Q6T33_E_TAKEN', ['Tatooine, Star_Destroyer'], 'space',
               'ELIM E as in q6 t25 (Star Destroyer at Tatooine, 82-char message after the take); t35 (b)'),
    'E_NOMSG': ('E', '', ['Tatooine, Star_Destroyer'], 'space',
                'E without the Game_Message after the take: the Lua hint text'),
    'E_NOFLEET': ('E', 'TEXT_Q6T33_E_TAKEN', [], 'space',
                  'E with no fleet: the local-player retreat of Change_Owner has nothing to move'),
    'E_SDBYSS': ('E', 'TEXT_Q6T33_E_TAKEN', ['Byss, Star_Destroyer'], 'space',
                 'E with the Star Destroyer at Byss, not Tatooine; t35 (a)'),
    'E_CURPLANET': ('E', 'TEXT_Q6T33_E_TAKEN', ['Tatooine, Star_Destroyer'], 'planet',
                    'E_BASE with the cursor resting on Tatooine: the encyclopedia pop-up'),
    'D_BASE': ('D', 'TEXT_Q6T33_D_GIVEN', [], 'space',
               'ELIM D as in q6 t25 (58-char message after the give)'),
    'D_LONGMSG': ('D', 'TEXT_Q6T33_LONG', [], 'space',
                  'D with a 150-char Game_Message after the give: does the hint gate scale with the text length'),
}

TEXTS = [
    ('TEXT_Q6T33_RUNNING_D', 'Q6T33 D: you own nothing; at 20 s the Rebel gets Abregado Rae. Click nothing.'),
    ('TEXT_Q6T33_RUNNING_E', 'Q6T33 E: Tatooine is yours at 10 s and neutral at 40 s. Click nothing.'),
    ('TEXT_Q6T33_E_GIVEN', 'Q6 PROBE ELIM E: the script gave you Tatooine'),
    ('TEXT_Q6T33_E_TAKEN', MSG_E_TAKEN),
    ('TEXT_Q6T33_D_GIVEN', MSG_D_GIVEN),
    ('TEXT_Q6T33_LONG', MSG_LONG),
    ('TEXT_Q6T33_DONE', 'Q6T33: 120 s passed and the campaign is still running. Quit (do not save).'),
    ('TEXT_Q6T33_BEGIN', 'Q6T33: probe plot loaded'),
] + [('TEXT_Q6T33_CAMP_%s_NAME' % k, 'Q6T33: %s' % k) for k in VARIANTS] + \
    [('TEXT_Q6T33_CAMP_%s_DESC' % k, 'q6 t33 probe %s: %s. Click nothing.' % (k, v[4])) for k, v in VARIANTS.items()]


def campaign_xml(variant):
    mode, _, forces, _, _ = VARIANTS[variant]
    key = 'Q6T33_' + variant
    ef = ''.join('\t\t<Starting_Forces> Empire, %s </Starting_Forces>\n' % f for f in forces)
    x = q6.CAMPAIGN.format(key=key, ai_victory='Galactic_All_Planets_Controlled', empire_forces=ef)
    # the template names its texts TEXT_Q6_CAMP_<key>_...; this kit's own ids
    return x.replace('TEXT_Q6_CAMP_%s_' % key, 'TEXT_Q6T33_CAMP_%s_' % variant)


def story_xml():
    ev = q6.elapsed_event('Q6T_Begin', '1', ('text', 'TEXT_Q6T33_BEGIN'))
    return ('<?xml version="1.0" ?>\n<!-- Q6 t33 probe (tools/probe_mod/q6_t33_elim_end) -->\n<Story>\n%s</Story>\n'
            % ev)


def prepare(out):
    if os.path.exists(out):
        if not os.path.exists(os.path.join(out, MARKER)):
            raise SystemExit('refusing to overwrite %s: not a Q6 t33 probe folder' % out)
        shutil.rmtree(out)
    os.makedirs(out)
    q6.write(os.path.join(out, MARKER),
             b'Q6 t33 probe mod (tools/probe_mod/q6_t33_elim_end); removed by cleanup_q6t33.cmd\r\n')


def build(game, out, source_zip=None, no_text=False):
    src = q6.Source(game, source_zip)
    prepare(out)
    cf, cf_src = src.read(q6.CAMPAIGNFILES)
    if cf is None:
        raise SystemExit('shipped CampaignFiles.xml not found')
    cft = cf.decode('latin-1')
    i = cft.index('</Campaign_Files>')
    cft = cft[:i] + '\t<File>Q6T33_Campaigns.xml</File>\r\n' + cft[i:]
    q6.write(os.path.join(out, 'Data', 'XML', 'CampaignFiles.xml'), cft.encode('latin-1'))
    lua = open(os.path.join(HERE, 'q6t33_story.lua'), encoding='ascii').read()
    camps = []
    story_dir = os.path.join(out, 'Data', 'Scripts', 'Story')
    for v, (mode, msg, _, _, _) in VARIANTS.items():
        key = 'Q6T33_' + v
        camps.append(campaign_xml(v))
        q6.write(os.path.join(out, 'Data', 'XML', 'Q6_Story_Plots_%s.xml' % key),
                 q6.crlf(q6.PLOTS.format(lua='Q6T33_Story_' + v, key=key)))
        q6.write(os.path.join(out, 'Data', 'XML', 'Q6_Story_%s.xml' % key), q6.crlf(story_xml()))
        text = lua.replace('@@VARIANT@@', v).replace('@@MODE@@', mode).replace('@@MSG@@', msg)
        assert not any(p in text for p in ('@@VARIANT@@', '@@MODE@@', '@@MSG@@'))
        q6.write(os.path.join(story_dir, 'Q6T33_Story_%s.lua' % v), q6.crlf(text))
    q6.write(os.path.join(out, 'Data', 'XML', 'Q6T33_Campaigns.xml'),
             q6.crlf('<?xml version="1.0"?>\n<Campaigns>\n\t<!-- Q6 t33 probe (tools/probe_mod/q6_t33_elim_end) -->\n'
                     '%s</Campaigns>\n' % ''.join(camps)))
    n_text = 0
    if not no_text:
        mt, _ = src.read(q6.MASTERTEXT)
        if mt is None:
            raise SystemExit('shipped MasterTextFile_English.dat not found (use --no-text for an offline check)')
        tmp = os.path.join(out, 'mt.tmp')
        q6.write(tmp, mt)
        keys = {k for k, _ in TEXTS}
        pairs = [kv for kv in dat_read(tmp) if kv[0] not in keys]
        os.remove(tmp)
        q6.write(os.path.join(out, 'Data', 'Text', 'MasterTextFile_English.dat'), dat_bytes(pairs + TEXTS))
        n_text = len(TEXTS)
    print('built %s (CampaignFiles from %s, %d campaigns, %d probe strings)' % (out, cf_src, len(VARIANTS), n_text))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default=ted_owners.DEFAULT_GAME)
    ap.add_argument('--out')
    ap.add_argument('--source-zip')
    ap.add_argument('--no-text', action='store_true')
    a = ap.parse_args()
    build(a.game, a.out or os.path.join(a.game, 'corruption', 'Mods', MOD_NAME), a.source_zip, a.no_text)


if __name__ == '__main__':
    main()
