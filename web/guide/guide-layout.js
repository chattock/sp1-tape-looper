// Presentation groups keep every original demonstration independently addressable.
export const related={
 'transport':['volume'],
 'replace':['delete'],
 'songs':['brightness','status-lights'],
 'first-loop':['connect','power','track-lights'],
 'open-page':['pickup'],
 'fx-2-3':['wah-resonance'],
 'fx-2-4':['echo-feedback'],
 'fx-3-3':['tremolo-shape','tremolo-spread'],
 'fx-3-4':['reverb-damping'],
 'placement':['center-placement'],
 'preset':['take-length','countdown','grid-record-lights'],
 'nudge':['nudge-lights','reset-timing'],
 'resize-repeat':['release-repeat','repeat-no-grid','repeat-chop'],
 'repeat-faders':['repeat-octave'],
 'tape-copy':['stop-page','print-speed','held-print'],
 'print-repeat':['flow-back']
};
export const nestedIds=new Set(Object.values(related).flat());
// Keep the old walkthrough addressable; resize-repeat now covers the full sequence.
export const hiddenIds=new Set(['catch','repeat-faders']);
export const groupIntros={
 'Page 5 / Global EQ':'Hold FN + T4 until Page 4 opens, then release both. Hold FN again and tap VOL+ once for Page 5. Release FN to use the page; tap FN to close.',
 'Page 6 / Stereo placement':'Hold FN + T4 until Page 4 opens, then release both. Hold FN again and tap VOL+ twice for Page 6. Release FN to use the page; tap FN to close.',
 'Page 7 / Take & timing':'Hold FN + T4 until Page 4 opens, then release both. Hold FN again and tap VOL+ three times for Page 7. Release FN to use the page; tap FN to close.',
 'Page 8 / Next take format':'Hold FN + T4 until Page 4 opens, then release both. Hold FN again and tap VOL+ four times for Page 8. Release FN to use the page; tap FN to close.',
 'Bounce what you hear':'A bounce records the playing mix onto another track: effects, chops, beat repeats and speed moves can become part of the new recording. Start with a tape copy; sampler print is an alternate way to capture it.',
 'Four heads, one loop':'An optional way to play: four heads read different positions in one recording. Your other recordings stay available.',
 'Pages & pickup':'Pages temporarily give the controls a different job. Open a page, adjust it, then tap FN to return. Effects stay set when you leave.'
};
export function introduction(playIcon){
 const play=(id,title,desc,n,attrs='')=>`<button class="start-lesson" data-start="${id}" ${attrs}><span class="start-number">${n}</span><span><strong>${title}</strong><small>${desc}</small></span><span class="start-play" aria-hidden="true">${playIcon}</span></button>`;
 return `<section class="guide-intro" id="start-here" data-section="0" aria-labelledby="intro-title">
 <p class="eyebrow">FIELD GUIDE</p><h1 id="intro-title">Overview</h1>
 <p class="intro-lead">Make a loop from incoming audio. Shape it with speed, chops and effects. Bounce the result onto another track and keep going.</p>
 <div class="orientation"><h2>Three ways to work.</h2><p>The same controls do different jobs depending on where you are.</p>
 <div class="context-grid"><div><h3>Track mode</h3><p>Four separate recordings. The faders mix their levels.</p><p class="context-entry">This is where you start.</p></div><div><h3>Four-head mode</h3><p>Four playheads reading one recording at different positions.</p><p class="context-entry">With a loop playing, hold FN and triple-tap PLAY to enter or return to tracks.</p><button class="context-show" data-context="heads" aria-label="Show me four-head mode">Show me <span aria-hidden="true">${playIcon}</span></button></div><div><h3>Control pages</h3><p>Effects, EQ, stereo placement, timing and format.</p><p class="context-entry">Hold FN + a track button to open a page. Tap FN to close.</p><button class="context-show" data-context="open-page" aria-label="Show me control pages">Show me <span aria-hidden="true">${playIcon}</span></button></div></div>
 <p class="context-note">Pages work in either track or four-head mode.</p>
 <details class="gesture-primer" hidden><summary>Four main gesture groups</summary><div class="gesture-grid"><div><strong>Controls alone</strong><span>For example: Faders mix tracks; the rocker changes tape speed.</span></div><div><strong>Hold FN</strong><span>For example: FN + rocker reshapes the loop window.</span></div><div><strong>Hold PLAY</strong><span>For example: PLAY + rocker catches a beat repeat.</span></div><div><strong>Hold PLAY + FN</strong><span>For example: Add the rocker for octave jumps.</span></div></div></details></div>
 <div class="starting-path"><h2>Quickstart</h2><p>Start here, then try the four creative examples. Each walkthrough is set up for you.</p>${play('power','Wake the SP-1','Hold FN for about 1.5 seconds.',1,'data-start-steps="2" data-start-title="Wake the SP-1"')}${play('first-loop','Record a loop','Bring sound into an empty track.',2)}<div class="start-shape"><div class="start-shape-heading"><span class="start-number">3</span><div><h3>Shape the loop</h3><p>Change its speed, choose a smaller part, and move through your recording.</p></div></div><div class="shape-options"><button data-start="speed" aria-label="Show speed controls"><span><strong>Speed</strong><small>Rocker up/down</small></span><span class="shape-play" aria-hidden="true">${playIcon}</span></button><button data-start="chop" aria-label="Show chop controls"><span><strong>Chop</strong><small>FN + rocker</small></span><span class="shape-play" aria-hidden="true">${playIcon}</span></button><button data-start="move-window" aria-label="Show window movement"><span><strong>Move</strong><small>FN + VOL− / VOL+</small></span><span class="shape-play" aria-hidden="true">${playIcon}</span></button></div><details class="shape-faders"><summary>Shape it with faders</summary><p>Hold FN: F1 sets start, F2 sets end, F3 moves the region, F4 adjusts size.</p><button class="context-show" data-start="four-faders" aria-label="Show fader window controls">Show me <span aria-hidden="true">${playIcon}</span></button></details></div>${play('fx-2-4','Add some effects','Open a page and add echo to your loop.',4)}${play('tape-copy','Bounce the result','Capture the playing mix onto another track.',5)}<p class="input-hint">On your SP-1, audio comes in over USB-C. Choose SP-1 as your phone or computer’s audio output.</p></div>
 </section>`;
}
