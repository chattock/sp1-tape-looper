// Presentation groups keep every original demonstration independently addressable.
export const related={
 'transport':['volume'],
 'replace':['delete'],
 'songs':['brightness','status-lights'],
 'first-loop':['connect','power','track-lights'],
 'open-page':['pickup'],
 'fx-1-1':['filter-damping-v4'],
 'fx-1-2':['chorus-span-v4'],
 'fx-3-1':['phaser-feedback-v4'],
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
 'Page 5 / Global EQ':'Hold FN + T4 until Page 4 opens, then release both. Press VOL+ once for Page 5. Tap FN to close.',
 'Page 6 / Stereo placement':'Hold FN + T4 until Page 4 opens, then release both. Press VOL+ twice for Page 6. Tap FN to close.',
 'Page 7 / Take & timing':'Hold FN + T4 until Page 4 opens, then release both. Press VOL+ three times for Page 7. Tap FN to close.',
 'Page 8 / Next take format':'Hold FN + T4 until Page 4 opens, then release both. Press VOL+ four times for Page 8. Tap FN to close.',
 'Bounce what you hear':'A bounce records the playing mix onto another track: effects, chops, beat repeats and speed moves can become part of the new recording. Start with a tape copy; sampler print is an alternate way to capture it.',
 'Four heads, one loop':'An optional way to play: four heads read different positions in one recording. Your other recordings stay available.',
 'Pages & pickup':'Pages temporarily give the controls a different job. Open a page, adjust it, then tap FN to return. Effects stay set when you leave.'
};
export function introduction(playIcon){
 const play=(id,title,desc,n,attrs='')=>`<button class="start-lesson" data-start="${id}" ${attrs}><span class="start-number">${n}</span><span><strong>${title}</strong><small>${desc}</small></span><span class="start-play" aria-hidden="true">${playIcon}</span></button>`;
 return `<section class="guide-intro" id="start-here" data-section="0" aria-labelledby="intro-title">
 <h1 id="intro-title">Overview</h1>
 <p class="intro-lead">Make a loop from incoming audio. Shape it with speed, chops and effects. Bounce the result onto another track and keep going.</p>
 <div class="orientation"><h2>Three ways to work.</h2><p>The same controls do different jobs depending on where you are.</p>
 <div class="context-grid"><div><h3>Track mode</h3><p>Four separate recordings. The faders mix their levels.</p><p class="context-entry">This is where you start.</p></div><div><h3>Four-head mode</h3><p>Four playheads reading one recording at different positions.</p><p class="context-entry">With a loop playing, hold FN and triple-tap PLAY to enter or return to tracks.</p><button class="context-show" data-context="heads" aria-label="Show me four-head mode">Show me <span aria-hidden="true">${playIcon}</span></button></div><div><h3>Control pages</h3><p>Effects, EQ, stereo placement, timing and format.</p><p class="context-entry">Hold FN + a track button to open a page. Tap FN to close.</p><button class="context-show" data-context="open-page" aria-label="Show me control pages">Show me <span aria-hidden="true">${playIcon}</span></button></div></div>
 <p class="context-note">Pages work in either track or four-head mode.</p>
 <details class="gesture-primer" hidden><summary>Four main gesture groups</summary><div class="gesture-grid"><div><strong>Controls alone</strong><span>For example: Faders mix tracks; the rocker changes tape speed.</span></div><div><strong>Hold FN</strong><span>For example: FN + rocker reshapes the loop window.</span></div><div><strong>Hold PLAY</strong><span>For example: PLAY + rocker catches a beat repeat.</span></div><div><strong>Hold PLAY + FN</strong><span>For example: Add the rocker for octave jumps.</span></div></div></details></div>
 <div class="starting-path"><h2>Quickstart</h2><p>Start here, then try the four creative examples. Each walkthrough is set up for you.</p>${play('power','Wake the SP-1','Hold FN for about 1.5 seconds.',1,'data-start-steps="2" data-start-title="Wake the SP-1"')}${play('first-loop','Record a loop','Bring sound into an empty track.',2)}<div class="start-shape"><div class="start-shape-heading"><span class="start-number">3</span><div><h3>Shape the loop</h3><p>Change its speed, choose a smaller part, and move through your recording.</p></div></div><div class="shape-options"><button data-start="speed" aria-label="Show speed controls"><span><strong>Speed</strong><small>ROCKER UP / ROCKER DOWN</small></span><span class="shape-play" aria-hidden="true">${playIcon}</span></button><button data-start="chop" aria-label="Show chop controls"><span><strong>Chop</strong><small>FN + rocker</small></span><span class="shape-play" aria-hidden="true">${playIcon}</span></button><button data-start="move-window" aria-label="Show window movement"><span><strong>Move</strong><small>FN + VOL− / VOL+</small></span><span class="shape-play" aria-hidden="true">${playIcon}</span></button></div><details class="shape-faders"><summary>Shape it with faders</summary><p>Hold FN: F1 sets start, F2 sets end, F3 moves the region, F4 adjusts size.</p><button class="context-show" data-start="four-faders" aria-label="Show fader window controls">Show me <span aria-hidden="true">${playIcon}</span></button></details></div>${play('fx-2-4','Add some effects','Open a page and add echo to your loop.',4)}${play('tape-copy','Bounce the result','Capture the playing mix onto another track.',5)}<p class="input-hint">Bring audio in over USB-C or Bluetooth. Choose SP-1 as your phone or computer’s audio output.</p></div>
 </section>`;
}

export function whatsNew(playIcon,cards,markup,rockers,effects){
 const ids=['takes-v4','bluetooth-v4','track-trim-v4','track-slide-v4','beats-per-bar-v4','escape-hatch-v4'];
 return `<section class="guide-intro whats-new" id="whats-new" data-section="-1" aria-labelledby="new-title"><h1 id="new-title">What’s New</h1><div class="tile-grid new-walkthroughs">${ids.map(id=>markup(cards.find(c=>c.id===id),false,'new-')).join('')}</div><article class="tile new-effects" id="new-effects"><h2>New effects &amp; tape color</h2><p><a data-guide-link="fx-rack-1-panel-1">Flanger</a>, <a data-guide-link="fx-rack-1-panel-3">Slow gear</a>, <a data-guide-link="fx-rack-2-panel-4">Lo-fi / Radio</a>, <a data-guide-link="fx-rack-3-panel-11">Reverb Freeze</a>, <a data-guide-link="fx-rack-4-panel-13">Compression</a>, <a data-guide-link="fx-rack-4-panel-14">Cassette / Vinyl / Pink / Minidisc noise</a> and <a data-guide-link="fx-rack-4-panel-15">separate Wow / Flutter</a>.</p><a data-guide-link="category-section-3">Explore effects →</a></article>${rockers(playIcon,cards,'new-')}</section>`;
}
