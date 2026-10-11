"use strict";
const fs=require("node:fs");
const assert=require("node:assert/strict");
const vm=require("node:vm");
const path=require("node:path");
const {File}=require("node:buffer");
const JSZip=require("../vendor/jszip-3.10.1.min.js");
global.File=File;
global.window={JSZip};
vm.runInThisContext(fs.readFileSync(path.join(__dirname,"..","forge-mod-packager.js"),"utf8"));
const {build,gather,slug}=window.ForgeModPackager;
function asset(name,body,rel){
  const item=new File([body],name,{type:"application/octet-stream"});
  if(rel)Object.defineProperty(item,"webkitRelativePath",{value:rel});
  return item;
}
(async()=>{
  for(const kind of ["Character","Kart","Wheels","Track"]){
    const models=kind==="Track"?
     [asset("course.lev","LVL"),asset("course.vrm","VRM")]:
     [asset("native.obj","o Test\nv 0 0 0\n"),asset("skin.png","Png data")];
    const pkg=await build({files:models,category:kind,title:"Demo "+kind,version:"1.0",description:"A community test mod"});
    assert(pkg.file.size>50,pkg);
    const zip=await JSZip.loadAsync(await pkg.file.arrayBuffer());
    const ini={Character:"character.ini",Kart:"kart.ini",Wheels:"wheels.ini",Track:"track.ini"}[kind];
    const section={Character:"character",Kart:"kart",Wheels:"wheel",Track:"track"}[kind];
    const text=await zip.file("demo_"+kind.toLowerCase()+"/"+ini).async("string");
    assert(text.includes("["+section+"]"),text);
    assert(text.includes("[forge_mod]"));
    assert(text.includes("type = "+kind));
    assert(zip.file("demo_"+kind.toLowerCase()+"/forge_package.json"));
    if(kind==="Track")assert(text.includes("lev = course.lev"));
    console.log("OK",kind, pkg.file.size, "bytes",Object.keys(zip.files).length,"entries");
  }
  const folder=[asset("kart.obj","o Test","author_kart/kart.obj"),
   asset("kart.png","PNG","author_kart/textures/kart.png")];
  const pkg=await build({files:folder,category:"Kart",title:"My Kart",version:"2.1"});
  const zip=await JSZip.loadAsync(await pkg.file.arrayBuffer());
  assert(zip.file("my_kart/textures/kart.png"));
  assert(!zip.file("my_kart/author_kart/kart.obj"));
  assert.throws(()=>gather([asset("hack.exe","oops")]),/Unsupported file type/);
  assert.throws(()=>gather([asset("bad.obj","a","../bad.obj")]),/Unsafe/);
  try{
    await build({files:[asset("missing.obj","a")],category:"Track",title:"Bad Track",version:"1.0"});
    throw Error("Missing track unexpectedly accepted");
  }catch(e){assert(String(e).includes("one .lev and one .vrm"))}
  console.log("WEB_MOD_PACKAGER_TESTS_OK");
})().catch(e=>{console.error(e);process.exitCode=1});
