import assert from 'node:assert/strict';import{Simulator}from '../web/field-guide/simulator.js';
for(let page=0;page<=8;page++)for(let mode=0;mode<9;mode++){
 let s=new Simulator();s.page=page;s.rockerMode=mode;s.startTake(3);s.button('PLAY',true);s.tick(80);s.button('PLAY',false);assert.equal(s.recording,null);assert.equal(s.playing,false);assert.equal(s.tracks[3].state,'empty');
 s=new Simulator();s.startTake(0);s.tracks[0].state='recording';s.button('PLAY',true);s.tick(80);s.button('PLAY',false);assert(s.recording);assert(s.playing);
}console.log('PASS model pending-arm pause/cancel across pages/rocker modes; active take remains recording');
