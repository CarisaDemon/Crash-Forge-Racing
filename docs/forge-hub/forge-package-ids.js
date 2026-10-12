/* Forge Hub: normalize web IDs INTO ZIP/INI before first submission.
   Keeps the same public ID across reviewed revisions. */
"use strict";
(function(){
 const types={
  Character:["character.ini","character"],Kart:["kart.ini","kart"],
  Wheels:["wheels.ini","wheel"],Track:["track.ini","track"],
  Modpack:["modpack.ini","modpack"]
 };
 function modifyIni(text,section,id,type,version){
  const rows=text.replace(/\r\n/g,"\n").split("\n");
  const matcher=/^\s*\[([^\]]+)\]\s*$/;
  // Native CFR wheels use [wheel]; some existing mods use [wheels].
  // Do not rely on the deprecated, mutable RegExp.$1 capture.
  const found=rows.some(line=>{
   const match=line.match(matcher);
   if(!match)return false;
   const actual=match[1].trim().toLowerCase();
   return actual===section||(section==="wheel"&&actual==="wheels");
  });
  if(!found)throw Error("ZIP lacks native ["+section+"] section.");
  const output=[];let inside=false;
  let character=false,hasBody=false;
  for(let i=0;i<rows.length;i++){
   const line=rows[i],m=line.match(matcher);
   if(m){
     inside=m[1].toLowerCase()==="forge_mod";
     character=m[1].toLowerCase()==="character";
     if(inside)continue;
   }
   if(inside)continue;
   // All characters are modular bodies. has_wheels never belongs to
   // character.ini; karts and independent wheel sets own their wheels.
   if(type==="Character"&&character&&/^\s*has_wheels\s*=/i.test(line))
     continue;
   if(type==="Character"&&character&&
      /^\s*(modular_body|separate_vehicle)\s*=/i.test(line))
      hasBody=true;
   output.push(line);
  }
  if(type==="Character"&&!hasBody){
    const at=output.findIndex(line=>/^\s*\[character\]\s*$/i.test(line));
    if(at>=0)output.splice(at+1,0,"modular_body = true");
  }
  return output.join("\n").trimEnd()+"\n\n[forge_mod]\n"+
      "id = "+id+"\ntype = "+type+"\nversion = "+version+
      "\nenabled = true\nsource = forge_hub\n";
 }
 function validName(name){
  return name&&!name.startsWith("/")&&!name.includes("\\")&&
     !name.split("/").some(x=>x==="."||x===".."||!x);
 }
 async function prepare(file,category,id,version){
  if(!window.JSZip)throw Error("ZIP packaging library is missing.");
  if(!/^[a-z0-9][a-z0-9_-]{0,63}$/.test(id))throw Error("Invalid web mod ID.");
  if(!/^[a-zA-Z0-9._+-]{1,28}$/.test(version))throw Error("Invalid mod version.");
  const zip=await window.JSZip.loadAsync(file,{checkCRC32:true});
  const entries=Object.values(zip.files).filter(x=>!x.dir);
  if(entries.length>2000)throw Error("Too many files in ZIP.");
  if(entries.some(x=>!validName(x.name)))throw Error("Unsafe ZIP path.");
  if(category==="Modpack"){
   const packs=entries.filter(x=>x.name==="modpack.ini");
   if(packs.length!==1)throw Error("Modpack needs modpack.ini at ZIP root.");
   const ini=await packs[0].async("string");
   zip.file("modpack.ini",modifyIni(ini,"modpack",id,category,version));
   const childTypes={racers:["character.ini","character","Character"],
                     karts:["kart.ini","kart","Kart"],
                     wheels:["wheels.ini","wheel","Wheels"],
                     tracks:["track.ini","track","Track"]};
   let count=0;
   const used=new Set([id]),moduleRoots=new Set();
   for(const item of entries){
    const seg=item.name.split("/");
    if(seg.length!==3||!childTypes[seg[0]]||seg[2]!==childTypes[seg[0]][0])continue;
    const child=seg[1];
    if(!/^[a-z0-9][a-z0-9_-]{0,63}$/.test(child)||used.has(child))
       throw Error("Duplicate or invalid component ID: "+child);
    used.add(child);count++;moduleRoots.add(seg[0]+"/"+child);
    const old=await item.async("string");
    zip.file(item.name,modifyIni(old,childTypes[seg[0]][1],child,
                                 childTypes[seg[0]][2],version));
   }
   if(count<2||count>60)throw Error("Modpack needs 2-60 individual modules.");
   // Reject unowned top-level files and cross-category INIs *before*
   // uploading, not after a user has already passed moderator review.
   const iniNames=new Set(["character.ini","kart.ini","wheels.ini","track.ini","modpack.ini"]);
   for(const item of entries){
     if(item.name==="modpack.ini")continue;
     const seg=item.name.split("/");
     if(seg.length<3||!childTypes[seg[0]]||
        !moduleRoots.has(seg[0]+"/"+seg[1]))
       throw Error("Modpack contains files outside an identified module: "+item.name);
     if(seg.length===3&&iniNames.has(seg[2].toLowerCase())&&
        seg[2]!==childTypes[seg[0]][0])
       throw Error("Modpack native INI belongs to a different module: "+item.name);
   }
  } else{
   const [iniName,section]=types[category]||[];
   if(!iniName)throw Error("Unsupported category.");
   const matches=entries.filter(x=>x.name.split("/").pop().toLowerCase()===iniName);
   if(matches.length!==1)throw Error("ZIP must contain exactly one "+iniName);
   const ini=await matches[0].async("string");
   zip.file(matches[0].name,modifyIni(ini,section,id,category,version));
  }
  const bytes=await zip.generateAsync({type:"uint8array",compression:"DEFLATE",
    compressionOptions:{level:6},streamFiles:false});
  if(bytes.byteLength>50*1024*1024)throw Error("Normalized ZIP exceeds 50 MiB.");
  const sha=Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256",bytes)),
     x=>x.toString(16).padStart(2,"0")).join("");
  return {file:new File([bytes],file.name,{type:"application/zip"}),
          hash:sha,id};
 }
 window.ForgePackageIds=Object.freeze({prepare});
})();
