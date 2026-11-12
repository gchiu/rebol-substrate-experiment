// Headless WASM verification of the browser playground. Run from this dir:
//   node test-node.mjs [glon.wasm]
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { createRequire } from "module";
const require = createRequire(import.meta.url);
const RuleText = require("./rules.js");
const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = process.argv[2] || path.join(HERE, "glon.wasm");
const LIBS = ["prelude.glon","strings.glon","strings-ext.glon","parse.glon","parse-driver.glon"];
function minify(s){return s.split(/\r?\n/).map(line=>{let out="",str=false;for(let i=0;i<line.length;i++){const c=line[i];if(c==='"'){str=!str;out+=c;continue;}if(!str&&c===';'&&line[i+1]===';')break;out+=c;}return out.trim();}).filter(Boolean).join("\n");}
const mod=await WebAssembly.compile(fs.readFileSync(WASM));
const libs=LIBS.map(f=>minify(fs.readFileSync(path.join(HERE,f),"utf8")));
const examples=JSON.parse(fs.readFileSync(path.join(HERE,"examples.json"),"utf8")).examples;
const enc=new TextEncoder(), td=new TextDecoder("latin1");
function frames(bytes){const out={captures:[]};let i=0;while(i<bytes.length){let nl=bytes.indexOf(10,i);if(nl<0)throw Error("frame");const l=td.decode(bytes.subarray(i,nl));i=nl+1;
 if(/^R [01]$/.test(l))out.result=Number(l.slice(2)); else if(l==="E")out.error=true; else if(/^C [0-9]+$/.test(l)){const n=Number(l.slice(2));const name=td.decode(bytes.subarray(i,i+n));i+=n+1;let n2=bytes.indexOf(10,i);const code=td.decode(bytes.subarray(i,n2));i=n2+1;let val=null;if(code==="N"){}else if(/^V [0-9]+$/.test(code)){const vn=Number(code.slice(2));val=td.decode(bytes.subarray(i,i+vn));i+=vn+1;}else throw Error("frame");out.captures.push({name,value:val});}else throw Error("line "+l);} return out;}
async function run(e){const lowered=RuleText.lower(e.definitions||"",e.rule);let ex,out=[];const view=()=>new Uint8Array(ex.memory.buffer);const imports={env:{host_print:(p,n)=>out.push(...view().subarray(p,p+n)),host_set_text:()=>{},host_set_html:()=>{},host_canvas_script:()=>{}}};const inst=await WebAssembly.instantiate(mod,imports);ex=inst.exports;if(ex.glon_init()!==0)throw Error("init");const put=s=>{const b=enc.encode(s),p=ex.glon_alloc(b.length);view().set(b,p);return[p,b.length];};for(let i=0;i<LIBS.length;i++)if(ex.glon_load(...put(libs[i]))!==0)throw Error("load "+LIBS[i]);const src=["web/reset","b: mk-string [ "+[...enc.encode(e.input)].join(" ")+" ]","web-r: judge [","  web-rules: "+lowered.registry,"  web-rule: "+lowered.rule,"  p/parse-mode web-rules b web-rule "+(e.all?1:0)+" "+(e.case?1:0),"]","web/report web-r"].join("\n");const before=out.length;if(ex.glon_run(...put(src))!==0)throw Error("run");return frames(new Uint8Array(out.slice(before)));}
let pass=0,fail=0;const ok=(c,n,d)=>{if(c){pass++;console.log("ok  "+n);}else{fail++;console.log("FAIL "+n+(d?"\n     "+d:""));}};
for(const e of examples){let r=await run(e);ok(r.result===(e.id==="failure"?0:1),e.id+" result",JSON.stringify(r));if(e.id==="copy")ok(r.captures.length===1&&r.captures[0].name==="captured"&&r.captures[0].value==="a","copy capture");if(e.id==="literal")ok(r.captures.length===0,"literal has no stale capture");}
let bad=await run({input:"a",definitions:"",rule:"[ missing end ]",all:false,case:false});ok(!!bad.error,"unknown named rule reports E");
try{RuleText.lower("", '[ 99 ]');ok(false,"malformed lowering is rejected");}catch(e){ok(/expected a rule/.test(e.message),"malformed lowering is rejected",e.message);}
try{RuleText.lower("", '[ set x "a" ]');ok(false,"unsupported set is rejected");}catch(e){ok(/unsupported operator/.test(e.message),"unsupported set is rejected",e.message);}
try{RuleText.lower('x: [ "a" ', '[ x end ]');ok(false,"malformed syntax is rejected");}catch(e){ok(/unterminated|expected/.test(e.message),"malformed syntax is rejected",e.message);}
const low=RuleText.lower('parens: [ "(" parens ")" | "x" ]', '[ parens end ]');ok(/reduce/.test(low.registry)&&/'parens/.test(low.rule),"named rule lowers",JSON.stringify(low));
const lo1=RuleText.lower("", '[ ["a" | "b"] "c" end ]');ok(lo1.rule.includes('0')&&lo1.rule.includes('reduce'),"nested alternatives lower",lo1.rule);
const lo2=RuleText.lower("", '[ 1 3 "a" end ]');ok(lo2.rule.includes('21 1 3'),"bounded repetition lowers",lo2.rule);
const lo3=RuleText.lower("", '[ charset "a-z0-9" end ]');ok(lo3.rule.includes('97 122')&&lo3.rule.includes('48 57'),"charset ranges lower",lo3.rule);
const lo4=RuleText.lower("", '[ "a\\\"b" end ]');ok(lo4.rule.includes('34'),"string escape lowers",lo4.rule);
const lo5=RuleText.lower("", '[ copy captured some "a" end ]');ok(lo5.rule.includes('9')&&lo5.rule.includes('99 97 112 116 117 114 101 100'),"capture name lowers",lo5.rule);
let empty=await run({input:"",definitions:"",rule:"[ none end ]",all:false,case:false});ok(empty.result===1,"empty input none");
console.log(pass+" passed, "+fail+" failed");process.exit(fail?1:0);
