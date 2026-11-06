import json,re,html,os
from reportlab.pdfgen import canvas
from reportlab.platypus import BaseDocTemplate,PageTemplate,Frame,Paragraph,Spacer,KeepTogether,PageBreak
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.colors import HexColor
from reportlab.lib.pagesizes import A4,landscape
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'../..'))
os.chdir(ROOT)
os.makedirs('output/pdf',exist_ok=True)
x=json.load(open('docs/print-v4/content.json'))
ink=HexColor('#202735'); muted=HexColor('#536175'); blue=HexColor('#2359a3'); pale=HexColor('#dde4ed')
def clean(s):
 s=str(s or '').translate(str.maketrans({'→':' > ','×':'x','−':'-','–':'-','—':' - ','’':"'",'“':'"','”':'"','…':'...','↔':' / '}))
 s=re.sub(r'(?<!ROCKER )\b(UP|DOWN)\b',r'ROCKER \1',s)
 return html.escape(s)
styles={
 'title':ParagraphStyle('title',fontName='Helvetica-Bold',fontSize=27,leading=31,textColor=ink,spaceAfter=14),
 'section':ParagraphStyle('section',fontName='Helvetica-Bold',fontSize=19,leading=23,textColor=ink,spaceAfter=13,keepWithNext=True),
 'head':ParagraphStyle('head',fontName='Helvetica-Bold',fontSize=11.5,leading=14,textColor=ink,spaceAfter=5),
 'gesture':ParagraphStyle('gesture',fontName='Helvetica-Bold',fontSize=9,leading=12,textColor=blue,spaceAfter=5),
 'body':ParagraphStyle('body',fontName='Helvetica',fontSize=9.3,leading=13,textColor=ink,spaceAfter=5),
 'note':ParagraphStyle('note',fontName='Helvetica',fontSize=8.5,leading=11.5,textColor=muted,spaceAfter=4),
 'label':ParagraphStyle('label',fontName='Helvetica-Bold',fontSize=7.5,leading=10,textColor=muted,spaceAfter=5),
}
def p(s,style='body'): return Paragraph(clean(s),styles[style])
def block(title,gesture,body,note='',label=''):
 a=[]
 if label:a.append(p(label.upper(),'label'))
 a += [p(title,'head'),p(gesture,'gesture'),p(body)]
 if note:a.append(p(note,'note'))
 a.append(Spacer(1,12))
 return KeepTogether(a)
W,H=A4
class Guide(BaseDocTemplate):
 def afterFlowable(self,f):
  if isinstance(f,Paragraph) and f.style.name=='section':
   title=f.getPlainText(); key='s'+str(self.page)+title[:8]
   self.canv.bookmarkPage(key);self.canv.addOutlineEntry(title,key,0)
def footer(c,d):
 c.setStrokeColor(pale);c.line(36,H-34,W-36,H-34)
 c.setFont('Helvetica-Bold',8);c.setFillColor(muted);c.drawString(36,H-24,'SP-1 / TAPE LOOPER 4.0')
 c.setFont('Helvetica',7.5);c.drawString(36,23,'Full guide  •  v4.0  •  6 Nov 2026');c.drawRightString(W-36,23,str(d.page))
full=os.path.join(ROOT,'output/pdf/TAPE-LOOPER-V4-FULL-GUIDE.pdf')
doc=Guide(full,pagesize=A4,title='SP-1 Tape Looper 4.0 - Full guide',author='Tape Looper',leftMargin=36,rightMargin=36,topMargin=48,bottomMargin=40)
gap=22; cw=(W-72-gap)/2
frames=[Frame(36,40,cw,H-88,id='left',leftPadding=0,rightPadding=0,topPadding=0,bottomPadding=0),Frame(36+cw+gap,40,cw,H-88,id='right',leftPadding=0,rightPadding=0,topPadding=0,bottomPadding=0)]
doc.addPageTemplates(PageTemplate(id='columns',frames=frames,onPage=footer))
story=[p('Tape Looper 4.0','title'),p('Full guide','section'),p('A playable reference for the SP-1. Gestures, takes, eight rocker modes, eight control pages, Bluetooth and USB Library access.'),Spacer(1,10)]
story += [block('Read the controls','T1-T4 = TRACK buttons / F1-F4 = faders','PLAY is the transport button. FN is the function button. VOL- and VOL+ are the two volume buttons. ROCKER UP and ROCKER DOWN refer to the side rocker.','A plus sign means hold controls together. “Tap” and “hold” are different gestures. Start from normal track mode unless a page or mode is named.'),block('Start here','Flash > Setup v4 > make music','Flash the current Tape Looper BIN with Solderless. Use Setup v4 on the Tape Looper site to check the filesystem and Bluetooth. Setup skips work already verified.','Keep USB connected during setup. If your computer selects SP-1 as its sound output, switch to your usual speakers while setting up.'),block('Your library','16 songs / 4 tracks per song','Four banks each contain four songs. With the v4 filesystem, each track can keep multiple takes; one lane is selected at a time.','Recording and file imports are limited to approximately eight minutes per take. The Library can prepare compatible audio and offers a clip selection for longer sources.'),block('USB Library','Stop/save > hold T2 + T3 for 3 seconds','Release the buttons. Open the mounted SP1 MUSIC drive in Finder or select that drive in the site’s Library. Add audio to the intended song/track; use takes to retain alternatives.','SP1 MUSIC is the mounted drive. Select its top-level folder when the browser asks for access. Do not select an individual song folder.'),block('Finish importing','Eject SP1 MUSIC in Finder','Keep USB connected while the device validates and imports audio, then returns to Tape Looper. T3 pulses during audio import. Large files or batches take longer.','Copy completion is not import completion. Avoid powering off or pressing mode gestures while it is saving. Keep an external copy of recordings you need.'),block('Find a control','Five chapters + rocker reference','01 Start & record\n02 Shape the tape\n03 Effects\n04 Place & time\n05 Repeat & bounce\n06 Rocker modes','PDF bookmarks jump to each chapter. The pocket guide is a separate six-panel reference.')]
story.append(PageBreak())
for cat in x['categories']:
 story += [Spacer(1,16),p(f"0{cat['id']} / {cat['title']}",'section')]
 last=None
 for c in [c for c in x['cards'] if c['category']==cat['id'] and c['id'] != 'speed' and (c.get('group') != 'Rocker modes' or c['id'] == 'restore-tape-v4')]:
  group='Tape reset' if c['id']=='restore-tape-v4' else c.get('group','')
  story.append(block(c['title'],c.get('gesture',''),c.get('body',''),c.get('note',''),group if group!=last else ''))
  last=group
story += [Spacer(1,16),p('06 / Rocker modes','section'),p('Open with FN + both VOL. Release FN; VOL- / VOL+ choose a slot. Tap FN to close. The selected mode remains active. Keep the page open for TRACK modifiers. Holding TRACK previews the normal track lights.'),Spacer(1,10)]
for r in x['rockerModes']:
 story.append(block(f"{r['slot']} / {r['name']}",r['summary'],'ROCKER UP: '+r['up']+'\nROCKER DOWN: '+r['down'],r['release']+' '+r['target']))
 story.append(KeepTogether([p(r['reset'],'gesture'),p(r['tip'],'note'),Spacer(1,10)]))
doc.build(story)
# Two-sided landscape A4 tri-fold. Outside left=back, center=fold-in, right=cover.
pocket=os.path.join(ROOT,'output/pdf/TAPE-LOOPER-V4-POCKET-GUIDE.pdf')
PW,PH=landscape(A4); panel=PW/3
c=canvas.Canvas(pocket,pagesize=(PW,PH));c.setTitle('SP-1 Tape Looper 4.0 - Pocket guide');c.setAuthor('Tape Looper')
ps={k:ParagraphStyle('p'+k,parent=v,fontSize=(8.5 if k=='body' else 8 if k in ['gesture','note'] else v.fontSize),leading=(11.1 if k=='body' else 10.3 if k in ['gesture','note'] else v.leading)) for k,v in styles.items()}
def panel_draw(n,title,rows,smallfooter):
 left=n*panel+19; width=panel-38;y=PH-29
 for text,sty in [(title,'section')]+rows:
  para=Paragraph(clean(text),ps[sty]);w,h=para.wrap(width,y-30);y-=h;para.drawOn(c,left,y);y-=5 if sty not in ['head'] else 2
 if y<35:raise ValueError((title,y))
 c.setFillColor(muted);c.setFont('Helvetica',6.7);c.drawString(left,20,smallfooter)
def row(h,g,b=''):
 return [(h,'head'),(g,'gesture')]+([(b,'body')] if b else [])
back=[]
back+=row('Bluetooth','Tap FN twice; hold the third press for 1 second.','All four TRACK lights slowly fade together while pairing. Choose SP-1 on your phone/computer. Record normally. USB and Bluetooth inputs can mix.')
back+=row('USB Library','Stop/save. Hold T2 + T3 for 3 seconds; release.','Choose the SP1 MUSIC drive in Finder or the site’s Library. Eject in Finder to finish importing; keep USB connected until Tape Looper returns.')
back+=row('Reset the tape','FN + double-tap PLAY','Restores normal speed and rocker transforms, including Magnet gaps and Scissors cuts. FX remain set.')
back+=row('Reset FX','On an FX page: FN + T1 + T4','Resets effects, EQ, rates, types and routing. Monitor mute is unchanged. Reset confirmation: all four TRACK lights solid briefly.')
back+= [('Print at 100% on A4, landscape, double-sided; flip on the short edge. Fold in three. This panel is the back.','note')]
fold=[]
fold+=row('Effects, EQ, placement & timing','Hold FN + T1-T4 to open pages 1-4.','Release both. VOL- / VOL+ move through all eight pages; tap FN to close. Move a fader through its saved value to take control.')
for h,b in [('1 / Color','Filter; Chorus / Flanger; Distortion; Gate / Slow gear.'),('2 / Texture','Crush / Lo-fi / Radio; Ring mod; Auto-wah; Echo.'),('3 / Motion & space','Phaser; Sweep; Tremolo; Reverb / Freeze.'),('4 / Tape','Drive; Tone / Compression (tap T2 to select; F2 adjusts); Noise; Wobble.'),('5 / EQ','Four bands; tap T1-T4 to flatten a band.'),('6 / Placement','F1-F4 pan tracks; tap TRACK to center.'),('7 / Timing','F1 grid offset; F2 take length; F3 beats per bar: 3 / 4 / 5 / 7 (saved per song). TRACK + fader nudges that track.'),('8 / Next take','Tap TRACK: solid = mono; blinking = stereo.')]:fold+=row(h,b)
cover=[('SP-1','label'),('Tape Looper 4.0','title'),('Pocket guide','head'),('T1-T4: TRACK buttons\nF1-F4: faders\n+ : hold together','body'),('Start in normal track mode unless a page is named.','note'),Spacer] if False else []
cover=[('SP-1','label'),('Tape Looper 4.0','title'),('Pocket guide','head'),('T1-T4: TRACK buttons. F1-F4: faders. A plus sign means hold together. Start in normal track mode unless a page is named.','body')]
cover+=row('Record','Tap an empty TRACK; sound starts recording.','Tap that TRACK again to finish. PLAY cancels an armed take and pauses; finish an active recording with TRACK.')
cover+=row('Play / stop','Tap PLAY. Hold and release PLAY to restart.')
cover+=row('Mix / mute','F1-F4: levels. Tap a loaded TRACK: mute/unmute.','VOL- / VOL+ set listening level.')
cover+=row('Power','Hold FN 1.5 seconds to wake.','Stop playback, then hold FN 2.5 seconds to power off.')
for i,(title,rows) in enumerate([('Connect & reset',back),('FX & control pages',fold),('',cover)]):panel_draw(i,title,rows,'Tape Looper 4.0 • 6 Nov 2026')
c.showPage()
a=[]
a+=row('Banks & songs','Hold FN + tap T1-T4.','Choose a bank; repeat the same button to step through its four songs. A longer hold opens a control page.')
a+=row('Takes','TRACK + ROCKER DOWN: cycle saved takes.','TRACK + ROCKER UP: New lane. An empty lane breathes when other takes exist. Hold FN during playback to count all takes.')
a+=row('Overdub','Hold loaded TRACK 0.6 seconds, release, then tap within 10 seconds.','Tap again to finish. The earlier take remains saved. FN cancels preparation.')
a+=row('Delete selected take','Double-tap TRACK; hold the second press.','Wait for saving to finish. This deletes the selected take, not every take.')
a+=row('Beat grid','Tap FN at least four times, starting on the downbeat.','Round BPM: after tapping, press and hold FN about 1 second. Clear grid: tap FN, then quickly press and hold about 1 second.')
a+=row('Fixed / variable','Briefly hold FN + PLAY; release both.','Fixed uses multiples of the first loop; variable allows independent lengths.')
b=[]
b+=row('Open / select','FN + both VOL; release FN. VOL- / VOL+ select.','Tap FN to close. Selected mode stays active.')
for h,g in [('1 Pitch','ROCKER UP / DOWN changes speed. Double-click: semitone; hold: sweep.'),('2 Wind','Hold UP forward / DOWN rewind. Release restores playback. On page, TRACK 1-4 + rocker: 0.5x / 1.5x / 3x / 4x. Double-tap TRACK to latch winding speed.'),('3 Bender','DOWN drags; UP surges. Release springs back.'),('4 Battery','DOWN adds sag; UP heals. Wear persists.'),('5 Sandpaper','DOWN adds damage; UP heals. Wear persists.'),('6 Magnet','Hold DOWN to erase a passing gap; UP undoes. Length stays the same.'),('7 Scissors / glue','DOWN marks start, then end; UP cancels / undoes. Up to eight cuts.'),('8 Manual tape head','Hold UP for normal contact; hold DOWN and use F1-F4 to scrub. Release is silent; PLAY still starts/stops.')]:b+=row(h,g)
b += [('Magnet / Scissors: bare rocker affects loaded tracks. Keep the rocker page open and hold TRACK to target one track. Scissors needs a full forward loop: no trim, Heads, repeat or recording.','note')]
d=[]
d+=row('Window','FN + F1 start / F2 end / F3 position / F4 size.','FN + ROCKER UP halves; DOWN doubles. FN + VOL- / VOL+ moves it. F4 fully up restores the F1-F3 region.')
d+=row('Track trim / slide','FN + PLAY + fader trims that track.','PLAY + fader slides the trimmed section. Raise the trim fader fully to restore length.')
d+=row('Whole loop / home','FN + PLAY + VOL-: whole forward loop and timing reset.','FN + PLAY + VOL+: home, keeping chop and direction.')
d+=row('Four heads','While playing: FN + triple-tap PLAY.','Repeat to exit. Hold a loaded TRACK to choose source; FN + faders moves heads.')
d+=row('Beat repeat','Hold PLAY + ROCKER UP.','UP halves / DOWN doubles. Release PLAY to return. Add FN for octave jumps. VOL- / VOL+ moves the repeat.')
d+=row('Bounce / print','Hold PLAY + tap destination TRACK: bounce.','Tap destination to finish. PLAY + hold destination: sampler print; release TRACK to finish. A loaded destination is replaced.')
d+=row('FX routing / input','PLAY + VOL-: INPUT; + VOL+: TRACKS. Double either: BOTH.','PLAY + both VOL toggles input monitoring. Muted input blocks ordinary recording.')
for i,(title,rows) in enumerate([('Record & takes',a),('Rocker modes',b),('Shape & perform',d)]):panel_draw(i,title,rows,'Inside / '+str(i+1)+' of 3')
c.save()
print(full);print(pocket)
