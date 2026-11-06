import {BackupSerial} from './serial.mjs';
export class Runtime extends BackupSerial{
 static async connect(port){await port.open({baudRate:115200,bufferSize:65536});const d=new Runtime(port);try{d.reader=port.readable.getReader();d.writer=port.writable.getWriter();await port.setSignals({dataTerminalReady:true});await d.identify();return d}catch(e){await d.close();throw e}}
 async response(command,pattern){await this.send(command);const seen=[];for(let n=0;n<256;n++){const s=await this.line();if(pattern.test(s))return s;if(s.includes('SETUP REFUSED'))throw Error('Stop recording/playback, finish saving and close pages, then try Connect again.');if(s.startsWith('BTID')&&s.includes('RUNTIME')&&!s.startsWith('BTID409 '))throw Error('This SP-1 needs the supplied 409 firmware.');seen.push(s);if(seen.length>12)seen.shift()}throw Error('Runtime response missing; flash the supplied firmware')}
 async identify(){return this.transact(async()=>{const s=await this.response('A409HELLO?',/^BTID409 RUNTIME identity=[a-f0-9]{16} takes=1$/);return {identity:/identity=([a-f0-9]{16})/.exec(s)[1],firmware:409}})}
 async status(){return this.transact(async()=>{const s=await this.response('A373CHECK?',/^STORAGE373 RESULT /);if(!/format=2\b/.test(s))throw Error('The musical library has not passed runtime admission');return s})}
 async enter(){return this.transact(async()=>{await this.response('A373SETUP?',/^BTID373 SETUP RESTART$/);await this.close()})}
}
