"""Build local review PDFs from the audited Field Guide snapshot.

Refresh first: node docs/print-v4/export_content.mjs [web-root] [YYYY-MM-DD]
Review outputs never overwrite the PDFs in docs/ or deploy the website.
"""
import argparse
import html
import json
import re
from datetime import date
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.platypus import BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, KeepTogether, CondPageBreak
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4, landscape

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output-dir', default=str(ROOT / 'output/pdf'))
args = parser.parse_args()
OUT = Path(args.output_dir).resolve()
OUT.mkdir(parents=True, exist_ok=True)
x = json.loads((ROOT / 'docs/print-v4/content.json').read_text())
revision = date.fromisoformat(x['revision']).strftime('%d %b %Y').lstrip('0')
cards = {c['id']: c for c in x['cards']}
ink, muted, blue, pale = [HexColor(c) for c in ('#202735', '#536175', '#2359a3', '#dde4ed')]
SITE = 'https://tapelooper.club'


def clean(value):
    text = str(value or '').translate(str.maketrans({
        '→': ' > ', '×': 'x', '−': '-', '–': '-', '—': '-', '’': "'", '‘': "'",
        '“': '"', '”': '"', '…': '...', '↔': ' / ', '·': ' / ', '\u00a0': ' ',
    }))
    # Keep every rocker direction explicit without duplicating an existing prefix.
    text = re.sub(r'(?<!ROCKER )\b(UP|DOWN)\b', r'ROCKER \1', text)
    return html.escape(text).replace('\n', '<br/>')


def linked(value):
    text = clean(value)
    return re.sub(r'(?<![\w/])(tapelooper\.club(?:/#[a-z-]+)?)',
                  lambda m: f'<a href="https://{m[1]}" color="#2359a3">{m[1]}</a>', text)


styles = {
    'title': ParagraphStyle('title', fontName='Helvetica-Bold', fontSize=27, leading=31, textColor=ink, spaceAfter=13),
    'section': ParagraphStyle('section', fontName='Helvetica-Bold', fontSize=18, leading=22, textColor=ink, spaceAfter=12, keepWithNext=True),
    'head': ParagraphStyle('head', fontName='Helvetica-Bold', fontSize=11.5, leading=14, textColor=ink, spaceAfter=5),
    'gesture': ParagraphStyle('gesture', fontName='Helvetica-Bold', fontSize=9, leading=12, textColor=blue, spaceAfter=5),
    'body': ParagraphStyle('body', fontName='Helvetica', fontSize=9.3, leading=13, textColor=ink, spaceAfter=5),
    'note': ParagraphStyle('note', fontName='Helvetica', fontSize=8.5, leading=11.5, textColor=muted, spaceAfter=4),
    'label': ParagraphStyle('label', fontName='Helvetica-Bold', fontSize=7.5, leading=10, textColor=muted, spaceAfter=5),
}


def p(text, style='body'):
    return Paragraph(linked(text), styles[style])


def block(title, gesture, body, note='', label=''):
    parts = [p(label.upper(), 'label')] if label else []
    parts += [p(title, 'head'), p(gesture, 'gesture'), p(body)]
    if note:
        parts.append(p(note, 'note'))
    return KeepTogether(parts + [Spacer(1, 8)])


W, H = A4


class Guide(BaseDocTemplate):
    def afterFlowable(self, flowable):
        if isinstance(flowable, Paragraph) and flowable.style.name == 'section':
            title = flowable.getPlainText()
            key = 'chapter-' + str(self.page) + '-' + str(len(title))
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(title, key, 0)


def footer(c, doc):
    c.setStrokeColor(pale)
    c.line(36, H - 34, W - 36, H - 34)
    c.setFont('Helvetica-Bold', 8)
    c.setFillColor(muted)
    c.drawString(36, H - 24, 'SP-1 / TAPE LOOPER 4.0')
    c.setFont('Helvetica', 7.5)
    c.drawString(36, 23, f'Full guide | Revised {revision} | tapelooper.club')
    c.linkURL(SITE, (36, 20, 330, 32), relative=0, thickness=0)
    c.drawRightString(W - 36, 23, str(doc.page))


full = OUT / 'TAPE-LOOPER-V4-FULL-GUIDE-REVIEW.pdf'
doc = Guide(str(full), pagesize=A4, title='SP-1 Tape Looper 4.0 - Full guide', author='Tape Looper',
            leftMargin=36, rightMargin=36, topMargin=48, bottomMargin=40)
gap = 22
cw = (W - 72 - gap) / 2
frames = [Frame(36 + i * (cw + gap), 40, cw, H - 88, id=str(i),
                leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0) for i in range(2)]
doc.addPageTemplates(PageTemplate(id='columns', frames=frames, onPage=footer))
story = [p('Tape Looper 4.0', 'title'), p('Full guide', 'section'),
         p('SP-1 reference: recording, unlimited takes, effects, tape controls and your library.'), Spacer(1, 11)]
story += [
    block('Read the controls', 'T1-T4 = TRACK buttons / F1-F4 = faders',
          'PLAY controls playback. FN is the function button. VOL- / VOL+ are the volume buttons. ROCKER UP / DOWN are the side rocker.',
          'A plus sign combines controls. Follow the words tap and hold; press order matters. Start in normal track mode unless a page or mode is named.'),
    block('Setup', 'tapelooper.club/#setup',
          'Connect SP-1 and follow Setup to install firmware and prepare the filesystem and Bluetooth. Verified steps are skipped.',
          'Solderless is also available from Setup. Keep USB connected; follow the backup and storage choices shown there.'),
    block('Your library', '4 banks x 4 songs / 4 tracks per song',
          'Each track has unlimited takes, limited by available storage. One take per track is selected for playback.',
          'Each take can hold approximately eight minutes. The Library converts compatible audio and lets you choose a shorter clip from a long file.'),
    block('USB Library', 'Stop/save > hold T2 + T3 for 3 seconds > release',
          'Wait for SP1 MUSIC to appear. T2 + T3 stay lit when the drive is ready. Open it in Finder / File Explorer, or choose it in the site\'s Library.',
          'Preparation can take time with a large library. In the browser, choose the top-level SP1 MUSIC drive, not a song folder.'),
    block('Finish importing', 'Eject SP1 MUSIC; keep USB connected',
          'After eject: T1 checks, T2 plans, T3 imports, T4 saves. Large files or batches can take several minutes. Wait for Tape Looper to return.',
          'A completed file copy can still need device import processing. Repeated alarm lights indicate an error; collect diagnostics before reflashing.'),
    block('Find a control', 'Six chapters',
          '01 Start & record\n02 Shape the tape\n03 Effects\n04 Place & time\n05 Repeat & bounce\n06 Rocker modes',
          'Use PDF bookmarks to jump between chapters. Interactive help: tapelooper.club/#guide. Live Companion: tapelooper.club/#live.'),
]
hidden = set(x['hiddenIds']) | {'speed', 'normal-speed'}
full_ids = []
for cat in x['categories']:
    story += [CondPageBreak(180), Spacer(1, 12), p(f"0{cat['id']} / {cat['title']}", 'section')]
    last = None
    selected = [card for card in x['cards'] if card['category'] == cat['id'] and card['id'] not in hidden
                and (card.get('group') != 'Rocker modes' or card['id'] == 'restore-tape-v4')]
    # Match the web guide's grouped reading order, including the later-added BPB entry.
    group_order = {group: i for i, group in enumerate(dict.fromkeys(card.get('group', '') for card in selected))}
    selected.sort(key=lambda card: group_order[card.get('group', '')])
    for card in selected:
        full_ids.append(card['id'])
        group = 'Tape reset' if card['id'] == 'restore-tape-v4' else card.get('group', '')
        note = card.get('note', '')
        if card['id'] == 'track-lights':
            note = ('Stopped, loaded/unmuted tracks stay solid. A refusal flash can mean saving is busy; '
                    'wait before recording or deleting. Alternating T1+T3 / T2+T4 indicates a fault; '
                    'keep USB connected and collect diagnostics before reflashing.')
        elif card['id'] == 'routing':
            note = ('INPUT prints input FX into ordinary takes. TRACKS leaves input dry. BOTH affects the heard mix; '
                    'ordinary takes stay dry. EQ always affects the whole mix. During a repeat, VOL- / VOL+ move its window.')
        body = card.get('body', '')
        if card['id'] == 'restore-tape-v4':
            body = 'Return to normal (1x) speed and clear temporary tape transformations, including Scissors cuts and Magnet gaps.'
        story.append(block(card['title'], card.get('gesture', ''), body, note,
                           group if group != last else ''))
        last = group
story += [CondPageBreak(235), Spacer(1, 12), p('06 / Rocker modes', 'section'),
          p('Hold FN + both VOL to open. Release FN; VOL- / VOL+ select a mode. Tap FN to close; the mode stays selected.'), Spacer(1, 10)]
for mode in x['rockerModes']:
    note = mode['release'] + ' ' + mode['target']
    tip = mode['tip']
    if mode['slot'] == 1:
        tip += ' Input recording locks speed; bounces allow speed changes.'
    story.append(block(f"{mode['slot']} / {mode['name']}", mode['summary'],
                       'ROCKER UP: ' + mode['up'] + '\nROCKER DOWN: ' + mode['down'],
                       note + '\n' + mode['reset'] + '\n' + tip))
doc.build(story)

# A4 landscape, two sides, six panels. Outside: back / fold-in / cover.
pocket = OUT / 'TAPE-LOOPER-V4-POCKET-GUIDE-REVIEW.pdf'
PW, PH = landscape(A4)
panel_width = PW / 3
c = canvas.Canvas(str(pocket), pagesize=(PW, PH))
c.setTitle('SP-1 Tape Looper 4.0 - Pocket guide')
c.setAuthor('Tape Looper')
ps = {
    'title': ParagraphStyle('p-title', parent=styles['title'], fontSize=26, leading=29),
    'section': ParagraphStyle('p-section', parent=styles['section'], fontSize=18, leading=21),
    'head': ParagraphStyle('p-head', parent=styles['head'], fontSize=10.5, leading=12.5),
    'gesture': ParagraphStyle('p-gesture', parent=styles['gesture'], fontSize=8.1, leading=10.3),
    'body': ParagraphStyle('p-body', parent=styles['body'], fontSize=8.5, leading=10.8),
    'note': ParagraphStyle('p-note', parent=styles['note'], fontSize=8, leading=10.3),
    'label': styles['label'],
}
panel_metrics = []


def panel_draw(index, title, rows, side):
    left = index * panel_width + 19
    width = panel_width - 38
    y = PH - 27
    if title:
        rows = [(title, 'section')] + rows
    for text, sty in rows:
        para = Paragraph(linked(text), ps[sty])
        _, height = para.wrap(width, PH)
        y -= height
        if y < 40:
            raise ValueError(f'{side}: {title or "Cover"} overflows at {text!r}; remaining y={y:.1f}')
        para.drawOn(c, left, y)
        y -= 2 if sty == 'head' else 5
    panel_metrics.append({'side': side, 'panel': index + 1, 'title': title or 'Cover', 'content_bottom': round(y, 1)})
    c.setFillColor(muted)
    c.setFont('Helvetica', 6.7)
    c.drawString(left, 20, f'4.0 | Revised {revision} | tapelooper.club')
    c.linkURL(SITE, (left, 17, left + width, 29), relative=0, thickness=0)


def row(title, gesture, body=''):
    return [(title, 'head'), (gesture, 'gesture')] + ([(body, 'body')] if body else [])


back = []
back += row('Bluetooth Support',
            'Triple-tap FN; hold the last press until the four track lights start fading, then release.',
            'Choose SP-1 in Bluetooth settings and as audio output. USB and Bluetooth input can mix.')
back += row('USB Library', 'Stop/save. Hold T2 + T3 for 3 seconds; release.',
            'Wait for SP1 MUSIC; T2 + T3 stay lit when ready. Eject after copying. Preparation/import can take minutes; keep USB connected until Tape Looper returns.')
back += row('Escape hatch', 'Hold FN + T1 + T2 for 2 seconds.',
            'Solid track lights confirm. Performance controls reset; recordings stay saved.')
back += row('Tape reset', 'Hold FN and double-tap PLAY.',
            'Restores 1x and clears tape transformations. FX and the selected rocker mode stay set.')
back += row('FX reset', 'On an FX page: hold FN; tap T1 + T4 together.',
            'Resets FX, EQ and routing to BOTH. Input mute stays set.')
back += [('Print at 100% on A4, landscape, double-sided. Flip on the short edge; fold in three. This is the back panel.', 'note')]

fold = row('Open a page', 'Hold FN + T1-T4; release both.',
           'VOL- / VOL+ move through Pages 1-8. Tap FN to close. Cross the saved fader value to take control.')
for title, text in [
    ('1 / Color', 'Filter; Chorus / Flanger; Distortion; Gate / Slow gear.'),
    ('2 / Texture', 'Crush / Lo-fi / Radio; Ring mod; Auto-wah; Echo.'),
    ('3 / Motion & space', 'Phaser; Sweep; Tremolo; Reverb / Freeze.'),
    ('4 / Tape', 'Drive; Tone / Compression; Noise; Wobble. Tap T2 to select Tone / Compression; F2 adjusts.'),
    ('5 / EQ', 'Center a fader, or tap its TRACK, for flat.'),
    ('6 / Placement', 'F1-F4 place tracks. Center the fader, or tap TRACK for exact center.'),
    ('7 / Timing', 'F1 grid offset; F2 take length; F3 beats per bar: 3 / 4 / 5 / 7. Hold TRACK + move its fader to nudge; the move is relative.'),
    ('8 / Next take', 'Tap TRACK: solid = mono; blinking = stereo.'),
]:
    fold += row(title, text)
fold += row('FX tempo / clear', 'Page open: hold FN; tap the timed FX TRACK 2+ times in time.',
            'Clear: hold FN + PLAY; tap that TRACK. Rate lanes: P1 T2/T4; P2 T4; P3 T1-T3; P4 T4.')

cover = [('SP-1', 'label'), ('Tape Looper 4.0', 'title'), ('Pocket guide', 'head'),
         ('T1-T4 = TRACK buttons. F1-F4 = faders. + combines controls; follow tap/hold and press order. Start in track mode unless a page or mode is named.', 'body')]
cover += row('Recording', 'Tap an empty TRACK to arm; tap again to finish.',
             'Sound starts recording without a grid; a grid uses its punch-in. PLAY cancels a waiting arm and pauses. Input recording locks tape speed.')
cover += row('Playback controls', 'Tap PLAY to pause/resume.', 'Hold PLAY about 0.5 seconds, then release to restart.')
cover += row('Mix / mute', 'F1-F4: levels. Tap a loaded TRACK: mute/unmute.', 'VOL- / VOL+ set listening level.')
cover += row('Power', 'Hold FN 1.5 seconds to wake.', 'Stop playback; hold FN 2.5 seconds to power off.')
cover += row('Setup / help', 'tapelooper.club/#setup', 'Field Guide: tapelooper.club/#guide')

for index, (title, rows) in enumerate([('Connect & resets', back), ('FX & control pages', fold), ('', cover)]):
    panel_draw(index, title, rows, 'Outside')
c.showPage()

a = row('Banks & songs', 'Hold FN; tap T1-T4.', 'Select a bank; repeat that button to cycle its four songs. A longer hold opens a page.')
a += row('Unlimited takes per track', 'Hold TRACK + ROCKER UP: New. TRACK + ROCKER DOWN: cycle takes.',
         'Close pages and leave Heads first. Earlier takes stay saved. Hold FN during playback to count takes.')
a += row('Overdub', 'Hold loaded TRACK 0.6 seconds, release, then tap within 10 seconds.',
         'Tap again to finish. Earlier take stays saved. FN cancels preparation.')
a += row('Take deletion', 'Double-tap TRACK; hold the second press.',
         'Deletes the selected take; other takes stay saved. Wait for saving to finish first.')
a += row('Beat grid', 'Tap FN 4+ times in time; start on the downbeat.',
         'Round BPM: tap 4+ times, then hold FN about 1 second. Clear grid: tap FN, quickly press again and hold about 1 second.')
a += row('Fixed / Variable', 'Hold FN + PLAY about 0.5 seconds; release both.',
         'Fixed uses base-loop multiples. Variable preserves independent lengths.')

b = row('Open / select', 'Hold FN + both VOL; release FN. VOL- / VOL+ select.', 'Tap FN to close; the mode stays selected.')
rocker_summaries = [
    'Click: speed step. Double-click: semitone. Hold: sweep. Range 0.25x-2x. FN + PLAY + rocker: octave jumps.',
    'Hold ROCKER UP forward / DOWN rewind; release to return. Page open: T1-T4 + rocker = 0.5x / 1.5x / 3x / 4x. Double-tap TRACK to latch.',
    'ROCKER DOWN stretches; UP surges. Release springs back.',
    'ROCKER DOWN adds sag; UP recovers. Wear stays on release.',
    'ROCKER DOWN roughens; UP reduces wear. Release keeps the wear.',
    'Hold ROCKER DOWN to leave a gap; tap UP to undo. Loop length stays.',
    'Tap ROCKER DOWN at start and end; UP cancels/undoes. Up to eight cuts.',
    'Hold ROCKER UP for tape contact. Hold DOWN + move F1-F4 for four heads. Release is silent.',
]
for mode, text in zip(x['rockerModes'], rocker_summaries):
    b += row(f"{mode['slot']} / {mode['name']}", text)
b += [('Magnet / Cut: with the rocker page open, hold TRACK to target one track; bare rocker targets loaded tracks. Cut needs full forward loops: no trim, Heads, repeat or recording.', 'note')]

d = row('Loop window', 'Hold FN: F1 start / F2 end / F3 position / F4 size.',
        'FN + rocker: UP halves / DOWN doubles. FN + VOL- / VOL+ moves it.')
d += row('Track trim / position', 'FN + PLAY + fader: lower to shorten; raise to lengthen.', 'PLAY + fader: lower to move the section later; raise to move earlier.')
d += row('Reset loop controls', 'Hold FN + PLAY; press VOL-.',
         'Clears window, trim and timing edits; take length OFF, 4 beats per bar. Recordings and FX stay unchanged.')
d += row('Reverse / isolate', 'Reverse: FN + TRACK first, then tap PLAY.', 'Isolate: FN + PLAY first, then hold TRACK. Release TRACK to restore the mix.')
d += row('Four-head mode', 'While playing: hold FN; triple-tap PLAY.', 'Repeat to exit. Hold a loaded TRACK to choose source; FN + faders position heads.')
d += row('Beat repeat', 'Hold PLAY; click ROCKER UP.', 'UP halves / DOWN doubles. Release PLAY to return. Add FN for octave jumps; VOL- / VOL+ moves the repeat.')
d += row('Bounce / sampler print', 'Hold PLAY; tap destination TRACK to bounce.',
         'Tap destination to finish. Hold it instead for sampler print; release to finish. Earlier takes stay saved.')
d += row('FX routing / input mute', 'Hold PLAY: VOL- = INPUT; VOL+ = TRACKS; double-tap either = BOTH.',
         'PLAY + both VOL mutes input, including its recording. Unmute before arming.')

for index, (title, rows) in enumerate([('Record & takes', a), ('Rocker modes', b), ('Shape & perform', d)]):
    panel_draw(index, title, rows, 'Inside')
c.save()
manifest = {'revision': x['revision'], 'full_guide_ids': full_ids, 'hidden_ids': sorted(hidden),
            'rocker_modes': [m['name'] for m in x['rockerModes']], 'pocket_panels': panel_metrics}
(OUT / 'guide-review-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(full)
print(pocket)
print(json.dumps(panel_metrics, indent=2))
