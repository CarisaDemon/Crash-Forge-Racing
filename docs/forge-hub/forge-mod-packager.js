/* Forge Hub browser Mod Packager: builds a native mod ZIP before private review.
   Nothing is uploaded or published by this module. Same format as Windows tool. */
"use strict";
(function () {
  const KINDS = Object.freeze({
    Character:["character.ini","character"],
    Kart:["kart.ini","kart"],
    Wheels:["wheels.ini","wheel"],
    Track:["track.ini","track"]
  });
  const EXT = new Set([".obj",".mtl",".png",".jpg",".jpeg",".bmp",".tga",".ini",
    ".cfg",".json",".txt",".md",".wav",".ogg",".cseq",".bin",".dds",".webp",
    ".gif",".xml",".dat",".gltf",".glb",".lev",".vrm"]);
  const MAX_ZIP=50*1024*1024,MAX_EXPANDED=200*1024*1024,MAX_ITEMS=2000;
  function slug(title){
    const base=String(title).normalize("NFKD").replace(/[\u0300-\u036f]/g,"")
      .toLowerCase().replace(/[^a-z0-9]+/g,"_").replace(/^_+|_+$/g,"").slice(0,58)||"community_mod";
    return /^(con|aux|prn|nul|mods|racers|karts|wheels|tracks|music|com[1-9]|lpt[1-9])$/.test(base)
      ? "mod_"+base : base;
  }
  function safeName(raw){
    const name=String(raw||"");
    if(!name||name.length>512||name.startsWith("/")||name.includes("\\")||
       name.includes(":")||/[\x00-\x1f\x7f]/.test(name))throw Error("Unsafe file path: "+name);
    const parts=name.split("/");
    if(parts.some(p=>!p||p==="."||p===".."||p.startsWith(".")))throw Error("Unsafe or hidden file: "+name);
    const filename=parts[parts.length-1];
    const dot=filename.lastIndexOf(".");
    const ext=dot<0?"":filename.slice(dot).toLowerCase();
    if(!EXT.has(ext))throw Error("Unsupported file type: "+name);
    return name;
  }
  function nativeIni(current,kind,title,version,modelName,lev,vrm){
    const [ini,section]=KINDS[kind];
    let original=current;
    if(!original){
      original="["+section+"]\nname = "+title+"\nenabled = true\n";
      if(kind==="Character"){
        original+="fallback_retail = crash\nengine = balanced\n"+
                  "modular_body = true\n"+
                  "\n[model]\nscale = 1.0\n\n[assets]\nasset_name = "+modelName+"\n";
      }else if(kind==="Kart"||kind==="Wheels"){
        original+="\n[model]\nasset_name = "+modelName+"\nscale = 1.0\n";
      }else{
        original+="format = native_lev_vrm\nlev = "+lev+"\nvrm = "+vrm+"\n";
      }
    }
    // CFR accepts both [wheel] and historical [wheels] wheel definitions.
    // Preserve the section written by the mod author rather than rewriting it.
    const nativeSection=kind==="Wheels"?"(?:wheel|wheels)":section;
    if(!new RegExp("^\\s*\\["+nativeSection+"\\]\\s*$","im").test(original))
      throw Error(ini+" must contain ["+section+"] section.");
    if(kind==="Character"){
      // Add only missing defaults. Explicitly authored values are preserved.
      // The OBJ is a modular character body, never a self-contained kart.
      const lines=original.replace(/\r\n/g,"\n").split("\n");
      let inside=false,hasBody=false;
      const clean=[];
      for(const line of lines){
        const header=line.match(/^\s*\[([^\]]+)\]\s*$/);
        if(header)inside=header[1].toLowerCase()==="character";
        if(inside&&/^\s*has_wheels\s*=/i.test(line))continue;
        if(inside&&/^\s*(modular_body|separate_vehicle)\s*=/i.test(line))
          hasBody=true;
        clean.push(line);
      }
      if(!hasBody){
        const start=clean.findIndex(line=>/^\s*\[character\]\s*$/i.test(line));
        clean.splice(start+1,0,"modular_body = true");
      }
      original=clean.join("\n");
    }
    let active=false;
    const result=[];
    for(const line of original.replace(/\r\n/g,"\n").split("\n")){
      const m=line.match(/^\s*\[([^\]]+)\]\s*$/);
      if(m)active=m[1].toLowerCase()==="forge_mod";
      if(!active)result.push(line);
    }
    return result.join("\n").trimEnd()+"\n\n[forge_mod]\n"+
      "id = local_"+slug(title).slice(0,55)+"\n"+
      "type = "+kind+"\nname = "+title+"\nversion = "+version+
      "\nsource = local\nenabled = true\n";
  }
  function gather(files){
    files=Array.from(files||[]);
    if(!files.length||files.length>MAX_ITEMS)throw Error("Select 1 to 2000 mod files.");
    // HTML folder chooser gives a common top-level directory. Strip only
    // that wrapper so the native INI and models land at the same module root.
    const rel=files.map(f=>String(f.webkitRelativePath||f.name));
    const first=rel[0].split("/")[0];
    const common=first&&!first.startsWith(".")&&!first.includes(":")&&
       rel.every(p=>p.includes("/")&&p.split("/")[0]===first)
      ?first+"/":"";
    const rows=files.map((file,index)=>({
      file,name:safeName(common?rel[index].slice(common.length):rel[index])
    }));
    let total=0;const keys=new Set();
    for(const row of rows){
      const key=row.name.toLowerCase();
      if(keys.has(key))throw Error("Duplicate mod file: "+row.name);
      keys.add(key);
      if(!Number.isSafeInteger(row.file.size)||row.file.size<0)throw Error("Bad file size.");
      total+=row.file.size;
      if(total>MAX_EXPANDED)throw Error("Selected files exceed browser packaging limit of 200 MiB.");
    }
    return rows;
  }
  /* Create a Modpack directly from a parent folder containing 2-60 mod folders.
     Each child stays an individual module, with its own native INI and ID.
     Prepared category roots (racers/id, karts/id, etc) are also supported. */
  async function buildModpack({files,title,version,author="",description=""}){
    if(!window.JSZip)throw Error("Bundled JSZip library is missing.");
    title=String(title||"").trim();
    version=String(version||"").trim();
    description=String(description||"").trim();
    author=String(author||"").trim();
    if(title.length<3||title.length>100||/[\r\n[\]]/.test(title))
      throw Error("Modpack name must contain 3-100 characters.");
    if(!/^[a-zA-Z0-9._+-]{1,28}$/.test(version))
      throw Error("Invalid Modpack version.");
    if(description.length<1||description.length>300||
       /[\r\n[\]]/.test(description))
      throw Error("Modpack description needs 1-300 characters.");
    if(author.length>75||/[\r\n[\]]/.test(author))
      throw Error("Modpack author is invalid.");
    const rows=gather(files);
    const roots={Character:"racers",Kart:"karts",Wheels:"wheels",Track:"tracks"};
    const native={Character:"character.ini",Kart:"kart.ini",
                  Wheels:"wheels.ini",Track:"track.ini"};
    const rootsByIni=Object.fromEntries(
      Object.keys(native).map(kind=>[native[kind],kind]));
    const prepared=rows.some(item=>/^(racers|karts|wheels|tracks)\/[^/]+\/(character|kart|wheels|track)\.ini$/i.test(item.name));
    const sourceGroups=new Map();
    for(const item of rows){
      if(item.name.toLowerCase()==="modpack.ini")continue;
      const parts=item.name.split("/");
      const min=prepared?3:2;
      if(parts.length<min)
        throw Error("Modpack files must be inside independent mod folders.");
      const source=prepared?parts.slice(0,2).join("/"):parts[0];
      const member=parts.slice(prepared?2:1).join("/");
      if(!member||member.startsWith("/")||member==="modpack.ini")
        throw Error("Invalid Modpack member path.");
      if(!sourceGroups.has(source))sourceGroups.set(source,[]);
      sourceGroups.get(source).push({file:item.file,member});
    }
    if(sourceGroups.size<2||sourceGroups.size>60)
      throw Error("Choose 2-60 separate mod folders for the Modpack.");
    const packId="local_"+slug(title);
    const seenIds=new Set([packId]);
    const zip=new window.JSZip();
    let members=0;
    for(const [source,group] of sourceGroups){
      const configs=group.filter(x=>Object.hasOwn(rootsByIni,x.member.toLowerCase()));
      if(configs.length!==1)
        throw Error(source+": needs exactly one root character.ini, kart.ini, wheels.ini or track.ini.");
      const kind=rootsByIni[configs[0].member.toLowerCase()];
      if(prepared&&source.split("/")[0].toLowerCase()!==roots[kind])
        throw Error(source+": source category does not match its native INI.");
      const iniText=await configs[0].file.text();
      const section=kind==="Wheels"?"(?:wheel|wheels)":KINDS[kind][1];
      if(!new RegExp("^\\s*\\["+section+"\\]\\s*$","im").test(iniText))
        throw Error(source+": "+configs[0].member+" has no native section.");
      const nativeName=iniText.match(/^\s*name\s*=\s*(.+)\s*$/im);
      const displayName=nativeName?nativeName[1].trim():source.split("/").pop();
      if(!displayName||displayName.length>100||/[\r\n[\]]/.test(displayName))
        throw Error(source+": invalid native mod name.");
      // Read IDs only from [forge_mod], not an unrelated native section.
      let inForge=false,inheritedId="",declaredType="";
      for(const line of iniText.replace(/\r\n/g,"\n").split("\n")){
        const head=line.match(/^\s*\[([^\]]+)\]\s*$/);
        if(head){inForge=head[1].toLowerCase()==="forge_mod";continue;}
        if(!inForge)continue;
        const value=line.match(/^\s*([a-z_]+)\s*=\s*(.*?)\s*$/i);
        if(!value)continue;
        if(value[1].toLowerCase()==="id")inheritedId=value[2].trim();
        if(value[1].toLowerCase()==="type")declaredType=value[2].trim();
      }
      if(declaredType&&declaredType.toLowerCase()!==kind.toLowerCase())
        throw Error(source+": native [forge_mod] category does not match "+kind);
      const identity=inheritedId||slug(source.split("/").pop());
      if(!/^[a-z0-9][a-z0-9_-]{0,63}$/.test(identity)||seenIds.has(identity))
        throw Error("Duplicate or invalid component ID: "+identity);
      seenIds.add(identity);
      const target=roots[kind]+"/"+identity+"/";
      const outputPaths=new Set();
      const names=group.map(x=>x.member.toLowerCase());
      if(kind==="Track"){
        if(names.filter(x=>x.endsWith(".lev")&&!x.includes("/")).length!==1||
           names.filter(x=>x.endsWith(".vrm")&&!x.includes("/")).length!==1)
          throw Error(source+": Track needs one .lev and one .vrm.");
      }else if(!names.some(x=>x.endsWith(".obj")||x.endsWith(".glb")||x.endsWith(".gltf"))){
        throw Error(source+": character/kart/wheels needs a model.");
      }
      for(const row of group){
        const name=row.member;
        if(outputPaths.has(name.toLowerCase()))
          throw Error("Duplicate path in "+source+": "+name);
        outputPaths.add(name.toLowerCase());
        if(row===configs[0]){
          const cleaned=nativeIni(iniText,kind,displayName,version,
                                 "", "", "");
          // nativeIni regenerates local metadata; assign the already
          // validated stable child identity, not the display-name slug.
          zip.file(target+name,cleaned.replace(
             /^id = [^\r\n]+$/m,"id = "+identity));
        }else{
          zip.file(target+name,await row.file.arrayBuffer(),{binary:true});
        }
        members++;
      }
    }
    const manifest="[modpack]\nname = "+title+
       "\ndescription = "+description+"\ncompatibility = cfr"+
       "\nauthor = "+author+"\nversion = "+version+"\nenabled = true"+
       "\n\n[forge_mod]\nid = "+packId+
       "\nname = "+title+"\ntype = Modpack"+
       "\nversion = "+version+"\nsource = local\nenabled = true\n";
    zip.file("modpack.ini",manifest);
    const bytes=await zip.generateAsync({type:"uint8array",compression:"DEFLATE",
      compressionOptions:{level:6},streamFiles:false});
    if(!bytes.length||bytes.byteLength>MAX_ZIP)
      throw Error("Modpack ZIP exceeds Forge Hub's 50 MiB limit.");
    const file=new File([bytes],slug(title)+"_modpack.zip",{type:"application/zip"});
    return {file,fileCount:members+1,componentCount:sourceGroups.size,
            size:file.size};
  }
  async function build({files,category,title,version,author="",description=""}){
    if(category==="Modpack")
      return buildModpack({files,title,version,author,description});
    if(!window.JSZip)throw Error("Bundled JSZip library is missing.");
    if(!KINDS[category])throw Error("Select Character, Kart, Wheels or Track.");
    title=String(title||"").trim();version=String(version||"").trim();
    if(title.length<3||title.length>100||/[\r\n[\]]/.test(title))
      throw Error("Mod name must contain 3-100 characters (no [ ] or newlines).");
    if(!/^[a-zA-Z0-9._+-]{1,28}$/.test(version))throw Error("Invalid mod version.");
    if(String(author).length>75||String(description).length>3000)
      throw Error("Author or description exceeds limit.");
    const rows=gather(files);
    const [ini]=KINDS[category];
    const iniNames=Object.values(KINDS).map(x=>x[0]);
    const native=rows.filter(x=>iniNames.includes(x.name.split("/").pop().toLowerCase()));
    if(native.length>1||(native.length===1&&native[0].name!==ini))
      throw Error("Pick a single native mod, with "+ini+" at its root.");
    const obj=rows.filter(x=>x.name.toLowerCase().endsWith(".obj"));
    if(category!=="Track"&&!obj.length)
      throw Error("This mod needs a native OBJ model. Use Model Importer first.");
    const lev=rows.filter(x=>x.name.toLowerCase().endsWith(".lev"));
    const vrm=rows.filter(x=>x.name.toLowerCase().endsWith(".vrm"));
    if(category==="Track"&&(lev.length!==1||vrm.length!==1||
        lev[0].name.includes("/")||vrm[0].name.includes("/")))
      throw Error("A track needs one .lev and one .vrm beside track.ini.");
    let original=native.length?await native[0].file.text():null;
    if(!original&&category!=="Track"&&!obj.some(x=>!x.name.includes("/")))
      throw Error("Place one OBJ in the folder root to auto-generate "+ini+".");
    const mainObj=obj.find(x=>!x.name.includes("/"));
    const stem=mainObj?mainObj.name.replace(/\.obj$/i,""):"";
    const name=slug(title);
    const zip=new window.JSZip();
    for(const row of rows){
      if(row.name===ini)continue;
      zip.file(name+"/"+row.name,await row.file.arrayBuffer(),{binary:true});
    }
    zip.file(name+"/"+ini,nativeIni(original,category,title,version,
             stem,lev[0]?.name||"",vrm[0]?.name||""));
    zip.file(name+"/forge_package.json",JSON.stringify({
      schema:1,name:title,type:category,version,author:String(author),
      description:String(description),id:"local_"+name,
      note:"Permanent Forge Hub ID is assigned on authenticated submission."
    },null,2));
    const bytes=await zip.generateAsync({type:"uint8array",compression:"DEFLATE",
      compressionOptions:{level:6},streamFiles:false});
    if(!bytes.length||bytes.byteLength>MAX_ZIP)
      throw Error("Generated ZIP exceeds Forge Hub's 50 MiB limit.");
    const file=new File([bytes],name+".zip",{type:"application/zip"});
    return {file,fileCount:rows.length+1,size:file.size};
  }
  function download(file){
    const link=document.createElement("a");
    const url=URL.createObjectURL(file);
    link.href=url;link.download=file.name;
    document.body.append(link);link.click();link.remove();
    setTimeout(()=>URL.revokeObjectURL(url),1200);
  }
  window.ForgeModPackager=Object.freeze({build,download,slug,gather});
})();
