// Pagina HTML embarcada do portal Wi-Fi, servida pelo radio interno do ESP32-WROOM-32D (folha 1/2).
// Mostra os reles RL2..RL5 e os bornes do CN1 da folha 2/2; sem nenhum recurso externo.
#pragma once

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#ifndef PROGMEM
#define PROGMEM
#endif

inline const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>DE-PURI-DI261924</title>
<style>
*{box-sizing:border-box}
:root{color-scheme:light dark;--bg:#eef1f5;--fg:#101820;--mut:#5b6b7a;--cd:#fff;--ln:#d5dde5;--ac:#0b62c4;--ok:#137a37;--bd:#c02626;--sk:#7c858d;--wb:#fdf0cf;--wf:#6a4c00;--br:#dde5ec}
@media(prefers-color-scheme:dark){:root{--bg:#0e1317;--fg:#e7eef4;--mut:#9aabb9;--cd:#161d23;--ln:#28323b;--ac:#4c9dfa;--ok:#3ec06c;--bd:#ff6b6b;--sk:#79838b;--wb:#3b3011;--wf:#f3d98d;--br:#212a32}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
.top{position:sticky;top:0;z-index:9;background:var(--bg);border-bottom:1px solid var(--ln)}
header{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;padding:9px 12px}
h1{font-size:16px;margin:0}
h2{font-size:15px;margin:0 0 9px;display:flex;justify-content:space-between;align-items:center;gap:8px}
.s,.mut,.hint,.lbl,.kv span{color:var(--mut);font-size:12.5px}
.warn{display:flex;align-items:center;gap:8px;padding:6px 12px;background:var(--wb);color:var(--wf);font-size:12.5px}
svg{width:16px;height:16px;flex:none}
.dot{display:inline-block;width:11px;height:11px;border-radius:50%;background:var(--sk);margin-right:6px}
.up{background:var(--ok)}.down{background:var(--bd)}
.grid{display:grid;gap:12px;padding:12px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));max-width:1180px;margin:0 auto}
.card{background:var(--cd);border:1px solid var(--ln);border-radius:12px;padding:12px}
.wide{grid-column:1/-1}
.badge{font-size:11.5px;font-weight:700;padding:3px 9px;border-radius:999px;background:var(--br);color:var(--mut);white-space:nowrap}
.b-ok{background:var(--ok);color:#fff}.b-bad{background:var(--bd);color:#fff}
.rows{display:flex;gap:10px}
.big{flex:1;text-align:center;background:var(--br);border-radius:10px;padding:8px 4px}
.lbl{display:block;font-weight:600}
.val{font-size:clamp(32px,10vw,50px);font-weight:700;font-variant-numeric:tabular-nums;line-height:1.1}
.stale{opacity:.45}
.kv{display:grid;grid-template-columns:repeat(auto-fit,minmax(134px,1fr));gap:0 12px;margin-top:9px;font-size:13px}
.kv div,.ax,.rl,.tst{border-bottom:1px dotted var(--ln)}
.kv div{display:flex;justify-content:space-between;gap:6px;padding:4px 0}
.kv b,.val{text-align:right}
.ax,.rl,.tst{padding:8px 0}
.rl,.tst,.axh,.ctl{display:flex;align-items:center;flex-wrap:wrap;gap:8px}
.axh{justify-content:space-between;font-size:13px}
.grow{flex:1 1 170px;min-width:0}
.bar{height:9px;background:var(--br);border-radius:6px;overflow:hidden;margin:7px 0 0}
.bar i{display:block;height:100%;width:0;background:var(--ac)}
.tst{font-size:13.5px}
.vd{min-width:66px;text-align:center;font-size:11.5px;font-weight:700;padding:4px 6px;border-radius:6px;color:#fff;background:var(--sk)}
.v-ok{background:var(--ok)}.v-bad{background:var(--bd)}
.ctl{margin-top:9px}
button,input{min-height:42px;border-radius:9px;font:inherit}
button{padding:0 14px;border:1px solid transparent;background:var(--ac);color:#fff;font-weight:600;cursor:pointer}
input{padding:0 10px;border:1px solid var(--ln);background:var(--bg);color:var(--fg);flex:1 1 120px;min-width:0}
.sec{background:0 0;color:var(--fg);border-color:var(--ln)}
p{margin:9px 0 0}
.lock{display:none}
.locked .lk{display:none}
.locked .lock{display:block}
pre{white-space:pre-wrap;background:var(--bg);border:1px solid var(--ln);border-radius:9px;padding:10px;max-height:320px;overflow:auto;font:12px ui-monospace,monospace;margin:10px 0 0}
.toast{position:fixed;left:50%;bottom:14px;transform:translateX(-50%);z-index:20;max-width:92vw;padding:10px 15px;border-radius:9px;background:var(--fg);color:var(--bg);font-size:13.5px}
</style></head>
<body class=locked>
<div class=top><header>
<div><h1>DE-PURI-DI261924 <span class=badge id=rev>rev -</span></h1>
<div class=s><span id=fw>v--</span> · <span id=sn>sem serie</span> · uptime <span id=up>0:00:00</span></div></div>
<div class=s><span class=dot id=dot></span><span id=conn>conectando...</span></div>
</header>
<div class=warn><svg viewBox="0 0 24 24"><path fill=currentColor d="M12 2 23 21H1Z"/><path fill="var(--wb)" d="M11 9h2v6h-2zm0 8h2v2h-2z"/></svg>
<span>Rádio ligado degrada a medida analógica em ±0,5 % FE. Desligue com <b>wifi off</b> antes de aferir as saídas.</span></div></div>
<main class=grid>

<div class="card wide lock"><b>Controles bloqueados.</b> <span class=mut>Somente leitura. Libere com o comando <b>wifi control on</b> no console serial.</span></div>

<section class=card><h2>Inclinação <span class=badge id=incB>--</span></h2>
<div class=rows>
<div class=big><span class=lbl>Eixo X</span><span class=val id=angX>--</span><span class=lbl>graus</span></div>
<div class=big><span class=lbl>Eixo Y</span><span class=val id=angY>--</span><span class=lbl>graus</span></div></div>
<div class=kv>
<div><span>Frames OK</span><b id=rOk>-</b></div><div><span>Erros CRC</span><b id=rCrc>-</b></div>
<div><span>Timeout</span><b id=rTo>-</b></div><div><span>Enquadramento</span><b id=rFr>-</b></div>
<div><span>Baud</span><b id=rBd>-</b></div><div><span>Protocolo</span><b id=rPr>-</b></div>
</div></section>

<section class=card><h2>Saídas analógicas <span class=badge id=aoM>--</span></h2>
<div id=aoL></div>
<div class="ctl lk"><button class=sec id=bMv>Modo tensão</button><button class=sec id=bMi>Modo corrente</button></div></section>

<section class=card><h2>Relés de limite</h2><div id=rlL></div>
<p class=hint>Cruzamento na IHM: LIM1 acende o LED serigrafado LED LIM3, LIM2 acende LED LIM1, LIM3 acende LED LIM2 e LIM4 acende LED LIM4.</p></section>

<section class=card><h2>Sistema</h2><div class=kv>
<div><span>Último reset</span><b id=sRst>-</b></div><div><span>Watchdog</span><b id=sWd>-</b></div>
<div><span>Chutes</span><b id=sWc>-</b></div><div><span>Período</span><b id=sWp>-</b></div>
<div><span>Display</span><b id=sDsp>-</b></div><div><span>SPI do DAC</span><b id=sSpi>-</b></div>
<div><span>Rádio</span><b id=sRad>-</b></div><div><span>SSID</span><b id=sSsid>-</b></div>
<div><span>IP</span><b id=sIp>-</b></div><div><span>Requisições</span><b id=sReq>-</b></div>
</div><div class="ctl lk"><button class=sec id=bSafe>Aplicar estado seguro</button></div></section>

<section class="card wide"><h2>Relatório de teste <span class=badge id=tRun>--</span></h2>
<div id=tL></div>
<div class=ctl><button id=bRep>Ver relatório</button><button class=sec id=bCsv>Copiar bloco CSV</button></div>
<pre id=rep hidden></pre></section>

</main>
<div class=toast id=toast hidden></div>
<script>
(function(){
function Q(i){return document.getElementById(i)}
function N(v,d){return (typeof v=="number"&&isFinite(v))?v:d}
function S(v,d){return (typeof v=="string"&&v.length)?v:d}
function O(v){return (v&&typeof v=="object")?v:{}}
function A(v){return Array.isArray(v)?v:[]}
function T(e,v){if(e){e.textContent=v}}
function p2(n){return (n<10?"0":"")+n}
function hms(m){var s=Math.floor(N(m,0)/1000);return Math.floor(s/3600)+":"+p2(Math.floor(s/60)%60)+":"+p2(s%60)}
function fx(v,d){return (typeof v=="number"&&isFinite(v))?v.toFixed(d):"--"}
function ni(v){return (typeof v=="number"&&isFinite(v))?String(Math.round(v)):"-"}
function clk(){var d=new Date();return p2(d.getHours())+":"+p2(d.getMinutes())+":"+p2(d.getSeconds())}
function el(t,c,x){var e=document.createElement(t);if(c){e.className=c}if(x!==undefined){e.textContent=x}return e}
function hz(v){var n=N(v,0);return n>=1e6?fx(n/1e6,2)+" MHz":ni(n)+" Hz"}
function cl(h){while(h.firstChild){h.removeChild(h.firstChild)}}
var LED={LIM1:"LED LIM3",LIM2:"LED LIM1",LIM3:"LED LIM2",LIM4:"LED LIM4"};
var tmr=null,tst=null,wait=1000,last="nunca",aoR=[],rlR=[],tsR=[];
function say(m){var t=Q("toast");T(t,m);t.hidden=false;if(tst){clearTimeout(tst)}tst=setTimeout(function(){t.hidden=true},4000)}
function sc(ms){if(tmr){clearTimeout(tmr)}tmr=setTimeout(tick,ms)}
function tick(){
var ac=new AbortController(),to=setTimeout(function(){ac.abort()},3000);
fetch("/api/status",{signal:ac.signal,cache:"no-store"}).then(function(r){
if(!r.ok){throw new Error("http")}
return r.json()}).then(function(j){
clearTimeout(to);last=clk();
Q("dot").className="dot up";T(Q("conn"),"online, atualizado "+last);
try{rd(j)}catch(e){say("resposta inesperada")}
wait=1000;sc(wait)},function(){
clearTimeout(to);
Q("dot").className="dot down";T(Q("conn"),"sem resposta, última "+last);
wait=Math.min(5000,wait+1000);sc(wait)})}
function eo(x){var m="";try{var o=JSON.parse(x);if(o&&typeof o.erro=="string"){m=o.erro}}catch(e){m=""}return m?"erro: "+m:"comando recusado"}
function po(p,b){
var ac=new AbortController(),to=setTimeout(function(){ac.abort()},3000);
fetch(p,{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:b,signal:ac.signal})
.then(function(r){return r.text().then(function(x){return {ok:r.ok,x:x}})})
.then(function(z){clearTimeout(to);say(z.ok?"comando aceito":eo(z.x));sc(150)},
function(){clearTimeout(to);say("falha ao enviar o comando");sc(150)})}
function gt(u,cb){
var ac=new AbortController(),to=setTimeout(function(){ac.abort()},5000);
fetch(u,{signal:ac.signal,cache:"no-store"}).then(function(r){return r.text()})
.then(function(x){clearTimeout(to);cb(x)},function(){clearTimeout(to);say("falha ao buscar o relatório")})}
function aoB(n){
var h=Q("aoL");cl(h);aoR=[];
for(var i=0;i<n;i++){
var bx=el("div","ax"),hd=el("div","axh"),nm=el("b"),st=el("span","mut");
hd.appendChild(nm);hd.appendChild(st);bx.appendChild(hd);
var br=el("div","bar"),fi=el("i");br.appendChild(fi);bx.appendChild(br);
var c=el("div","ctl lk"),ic=el("input"),iv=el("input");
ic.type="number";ic.min="0";ic.max="65535";ic.step="1";ic.placeholder="code 0..65535";
iv.type="number";iv.step="0.001";iv.placeholder="valor eng.";
var bc=el("button","","Code"),bv=el("button","","Valor"),bz=el("button","sec","Zerar");
(function(k,fc,fv){
bc.onclick=function(){var v=parseInt(fc.value,10);if(!isFinite(v)){say("code inválido: 0 a 65535");return}
po("/api/ao","axis="+k+"&code="+Math.max(0,Math.min(65535,v)))};
bv.onclick=function(){var v=parseFloat(fv.value);if(!isFinite(v)){say("informe um valor numérico");return}
po("/api/ao","axis="+k+"&value="+encodeURIComponent(String(v)))};
bz.onclick=function(){po("/api/ao","axis="+k+"&code=0")}})(i,ic,iv);
c.appendChild(ic);c.appendChild(bc);c.appendChild(iv);c.appendChild(bv);c.appendChild(bz);
bx.appendChild(c);h.appendChild(bx);aoR.push({nm:nm,st:st,fi:fi})}}
function rlB(n){
var h=Q("rlL");cl(h);rlR=[];
for(var i=0;i<n;i++){
var ro=el("div","rl"),inf=el("div","grow"),nm=el("b"),de=el("div","mut"),pi=el("span","badge","--"),c=el("div","ctl lk"),b=el("button");
inf.appendChild(nm);inf.appendChild(de);c.style.margin="0";c.appendChild(b);
ro.appendChild(inf);ro.appendChild(pi);ro.appendChild(c);h.appendChild(ro);
var st={nm:nm,de:de,pi:pi,b:b,on:false};
(function(k,s){b.onclick=function(){po("/api/relay","index="+k+"&state="+(s.on?"0":"1"))}})(i,st);
rlR.push(st)}}
function tsB(n){
var h=Q("tL");cl(h);tsR=[];
for(var i=0;i<n;i++){
var ro=el("div","tst"),v=el("span","vd","--"),nm=el("span","grow");
ro.appendChild(v);ro.appendChild(nm);h.appendChild(ro);tsR.push({v:v,nm:nm})}}
function rd(j){
j=O(j);
var r=O(j.radio),w=O(j.wdt),a=O(j.ao),s=O(j.rs485),t=O(j.teste),i,e,d,g;
T(Q("fw"),"v"+S(j.fw,"--"));T(Q("rev"),"rev "+S(j.rev,"-"));
T(Q("sn"),S(j.serie,"sem serie"));T(Q("up"),hms(j.uptimeMs));
document.body.classList.toggle("locked",r.controle!==true);
var ok=s.anguloValido===true,bg=Q("incB");
T(bg,ok?"dado válido":"sem dado válido");bg.className="badge "+(ok?"b-ok":"b-bad");
T(Q("angX"),fx(s.anguloX,1));T(Q("angY"),fx(s.anguloY,1));
Q("angX").className=Q("angY").className=ok?"val":"val stale";
T(Q("rOk"),ni(s.framesOk));T(Q("rCrc"),ni(s.crc));T(Q("rTo"),ni(s.timeout));
T(Q("rFr"),ni(s.enquadramento));T(Q("rBd"),ni(s.baud));T(Q("rPr"),S(s.protocolo,"-"));
var md=S(a.modo,"--"),un=md=="corrente"?" mA":" V",ax=A(a.eixos);
T(Q("aoM"),md);
if(aoR.length!=ax.length){aoB(ax.length)}
for(i=0;i<ax.length;i++){
e=O(ax[i]);d=N(e.code,0);
T(aoR[i].nm,"Eixo "+S(e.nome,String(i)));
T(aoR[i].st,md+" | code "+ni(d)+" | "+(e.calibrado===true?fx(e.valor,3)+un:"não calibrado"));
aoR[i].fi.style.width=Math.max(0,Math.min(100,d*100/65535))+"%"}
var rl=A(j.reles);
if(rlR.length!=rl.length){rlB(rl.length)}
for(i=0;i<rl.length;i++){
e=O(rl[i]);g=S(e.net,"-");d=e.ligado===true;
rlR[i].on=d;T(rlR[i].nm,g);
T(rlR[i].de,S(e.rele,"-")+" | bornes "+S(e.bornes,"-")+" | acende "+(LED[g]||"LED --"));
T(rlR[i].pi,d?"LIGADO":"desligado");rlR[i].pi.className="badge "+(d?"b-ok":"");
T(rlR[i].b,d?"Desligar":"Ligar")}
T(Q("sRst"),S(j.resetReason,"-"));
T(Q("sWd"),w.chutando===true?"chutando":"parado");
T(Q("sWc"),ni(w.chutes));T(Q("sWp"),ni(w.periodoMs)+" ms");
T(Q("sDsp"),S(j.display,"-"));T(Q("sSpi"),hz(a.spiHz));
T(Q("sRad"),S(r.modo,"-"));T(Q("sSsid"),S(r.ssid,"-"));
T(Q("sIp"),S(r.ip,"-"));T(Q("sReq"),ni(r.req));
var ru=t.emExecucao===true,tb=Q("tRun");
T(tb,ru?"em execução":"parado");tb.className="badge "+(ru?"b-ok":"");
var it=A(t.itens);
if(tsR.length!=it.length){tsB(it.length)}
for(i=0;i<it.length;i++){
e=O(it[i]);g=S(e.veredito,"");
T(tsR[i].nm,S(e.id,"?")+"  "+S(e.nome,"sem nome"));
T(tsR[i].v,g?g:"NAO EXEC");
tsR[i].v.className="vd "+(g=="PASS"?"v-ok":(g=="FAIL"?"v-bad":""))}}
function fb(x){
var a=document.createElement("textarea");a.value=x;a.setAttribute("readonly","");
a.style.position="fixed";a.style.top="-999px";document.body.appendChild(a);a.select();
var d=false;try{d=document.execCommand("copy")}catch(e){d=false}
document.body.removeChild(a);
if(d){say("bloco CSV copiado")}else{var p=Q("rep");p.hidden=false;T(p,x);say("copie o bloco abaixo")}}
function cp(x){
if(navigator.clipboard&&navigator.clipboard.writeText){
navigator.clipboard.writeText(x).then(function(){say("bloco CSV copiado")},function(){fb(x)})}else{fb(x)}}
Q("bMv").onclick=function(){po("/api/mode","mode=v")};
Q("bMi").onclick=function(){po("/api/mode","mode=i")};
Q("bSafe").onclick=function(){po("/api/safe","")};
Q("bRep").onclick=function(){gt("/api/report",function(x){var p=Q("rep");p.hidden=false;T(p,x)})};
Q("bCsv").onclick=function(){gt("/api/report?format=csv",cp)};
tick();
})();
</script></body></html>
)HTML";
