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
        original+="fallback_retail = crash\nengine = balanced\nhas_wheels = false\n"+
                  "\n[model]\nscale = 1.0\n\n[assets]\nasset_name = "+modelName+"\n";
      }else if(kind==="Kart"||kind==="Wheels"){
        original+="\n[model]\nasset_name = "+modelName+"\nscale = 1.0\n";
      }else{
        original+="format = native_lev_vrm\nlev = "+lev+"\nvrm = "+vrm+"\n";
      }
    }
    if(!new RegExp("^\\s*\\["+section+"\\]\\s*$","im").test(original))
      throw Error(ini+" must contain ["+section+"] section.");
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
  async function build({files,category,title,version,author="",description=""}){
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
