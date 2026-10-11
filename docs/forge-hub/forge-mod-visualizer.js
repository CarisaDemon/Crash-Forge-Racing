/* Native model preview inside Forge Hub Mod Packager.
   Read-only visualization (OBJ + MTL source files, or native LEV geometry).
   Does not claim to fully decode PS1 VRM texture atlases or collisions. */
"use strict";
(function () {
  const MAX_FACES=6000,MAX_VERTS=100000;
  function parseObj(text){
    if(typeof text!=="string"||text.length>50*1024*1024)
      throw Error("Invalid or oversized OBJ");
    const vertices=[],faces=[];
    for(const line of text.split(/\r?\n/)){
      const parts=line.trim().split(/\s+/);
      if(parts[0]==="v"&&parts.length>=4){
        if(vertices.length>=MAX_VERTS)break;
        const v=parts.slice(1,4).map(Number);
        if(v.every(Number.isFinite))vertices.push(v);
      }else if(parts[0]==="f"&&parts.length>=4&&faces.length<MAX_FACES){
        const corners=[];
        for(const raw of parts.slice(1,16)){
          const index=Number(raw.split("/")[0]);
          const next=index>0?index-1:vertices.length+index;
          if(!Number.isInteger(next)||next<0||next>=vertices.length){
            corners.length=0;break;
          }
          corners.push(next);
        }
        for(let i=1;i<corners.length-1&&faces.length<MAX_FACES;i++){
          faces.push([corners[0],corners[i],corners[i+1]]);
        }
      }
    }
    if(!vertices.length||!faces.length)throw Error("OBJ has no previewable faces");
    return {vertices,faces,colors:null,kind:"OBJ"};
  }
  function parseLev(buffer){
    const d=new DataView(buffer);
    if(d.byteLength<4096||d.byteLength>128*1024*1024)
      throw Error("Invalid LEV file size");
    const base=4;
    const structure=d.getUint32(0,true)&0x7fffffff;
    if(structure<256||structure>d.byteLength-4)
      throw Error("Invalid LEV structure");
    const header=base+d.getUint32(base,true);
    if(header<68||header+32>d.byteLength)throw Error("Invalid LEV mesh header");
    const quads=d.getUint32(header,true), nVerts=d.getUint32(header+4,true);
    const quadStart=base+d.getUint32(header+12,true);
    const vertStart=base+d.getUint32(header+16,true);
    if(!quads||quads>150000||!nVerts||nVerts>MAX_VERTS||
       quadStart<68||vertStart<68||
       quadStart+quads*92>d.byteLength||vertStart+nVerts*16>d.byteLength)
      throw Error("Invalid LEV mesh arrays");
    const vertices=[],colors=[];
    for(let i=0;i<nVerts;i++){
      const pos=vertStart+i*16;
      vertices.push([d.getInt16(pos,true),-d.getInt16(pos+2,true),d.getInt16(pos+4,true)]);
      colors.push([d.getUint8(pos+10),d.getUint8(pos+9),d.getUint8(pos+8)]);
    }
    const faces=[];
    const stride=Math.max(1,Math.ceil(quads/Math.max(1,MAX_FACES/8)));
    for(let i=0;i<quads;i+=stride){
      const pos=quadStart+i*92;
      const idx=Array.from({length:9},(_,j)=>d.getUint16(pos+j*2,true));
      if(idx.some(n=>n>=nVerts))continue;
      for(const [a,b,c,e] of [[0,1,4,3],[1,2,5,4],[3,4,7,6],[4,5,8,7]]){
        faces.push([idx[a],idx[b],idx[c]],[idx[a],idx[c],idx[e]]);
      }
    }
    if(!faces.length)throw Error("No valid LEV quad faces");
    return {vertices,faces,colors,kind:"LEV"};
  }
  function browserPreview(canvas,status,thumbs){
    if(!canvas||!status||!thumbs)throw Error("Mod preview DOM missing");
    const ctx=canvas.getContext("2d");
    if(!ctx)throw Error("2D preview canvas unavailable");
    let model=null,modelFiles=[],kind="Character";
    let yaw=.65,pitch=.45,zoom=1,drag=null,tint=[170,184,200];
    let xmid=0,ymid=0,zmid=0,span=1;
    let urls=[],serial=0;
    function dimensions(){
      if(!model)return;
      const v=model.vertices;
      const bounds=[0,1,2].map(j=>[
        Math.min(...v.map(x=>x[j])),Math.max(...v.map(x=>x[j]))]);
      [xmid,ymid,zmid]=bounds.map(([a,b])=>(a+b)/2);
      span=Math.max(1,...bounds.map(([a,b])=>b-a));
    }
    // Avoid spreading large user-supplied arrays onto the JS argument stack.
    function fit(){
      if(!model)return;
      const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];
      for(const v of model.vertices)for(let i=0;i<3;i++){
        min[i]=Math.min(min[i],v[i]);max[i]=Math.max(max[i],v[i]);
      }
      [xmid,ymid,zmid]=min.map((v,i)=>(v+max[i])/2);
      span=Math.max(1,...min.map((v,i)=>max[i]-v));
    }
    function draw(){
      const w=canvas.width,h=canvas.height;
      ctx.fillStyle="#0a121c";ctx.fillRect(0,0,w,h);
      ctx.strokeStyle="#27323f";ctx.lineWidth=1;
      for(let i=0;i<w;i+=50){ctx.beginPath();ctx.moveTo(i,h-15);ctx.lineTo(i+55,h-65);ctx.stroke()}
      if(!model){
        ctx.fillStyle="#e6ac78";ctx.font="15px sans-serif";
        ctx.fillText("Select native OBJ or LEV assets to preview",22,h/2);
        return;
      }
      const cy=Math.cos(yaw),sy=Math.sin(yaw),cp=Math.cos(pitch),sp=Math.sin(pitch);
      const factor=Math.min(w,h)*3.0*zoom;
      const view=model.vertices.map(v=>{
        const x=(v[0]-xmid)/span,y=(v[1]-ymid)/span,z=(v[2]-zmid)/span;
        const xo=cy*x+sy*z,zo=-sy*x+cy*z,yo=cp*y-sp*zo,depth=sp*y+cp*zo;
        const camera=Math.max(.5,4.6-depth);
        return [w/2+factor*xo/camera,h/2-factor*yo/camera,depth];
      });
      const triangles=[];
      for(const f of model.faces){
        const a=view[f[0]],b=view[f[1]],c=view[f[2]];
        if(!a||!b||!c)continue;
        if(Math.max(a[0],b[0],c[0]) < -20||Math.min(a[0],b[0],c[0])>w+20||
           Math.max(a[1],b[1],c[1]) < -20||Math.min(a[1],b[1],c[1])>h+20)continue;
        const color=model.colors? [0,1,2].map(i=>(model.colors[f[0]][i]+
           model.colors[f[1]][i]+model.colors[f[2]][i])/3):tint;
        triangles.push({d:(a[2]+b[2]+c[2])/3,points:[a,b,c],color});
      }
      triangles.sort((a,b)=>a.d-b.d);
      for(const triangle of triangles){
        const [a,b,c]=triangle.points;
        const winding=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
        const lit=winding<0?.82:.92;
        const rgb=triangle.color.map(c=>Math.min(255,Math.round(c*lit)+10));
        ctx.fillStyle="rgb("+rgb.join(",")+")";
        ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(b[0],b[1]);
        ctx.lineTo(c[0],c[1]);ctx.closePath();ctx.fill();
      }
      ctx.fillStyle="#ffad6f";ctx.font="bold 13px sans-serif";
      ctx.fillText(kind.toUpperCase()+" • "+model.kind+" PREVIEW",12,21);
    }
    canvas.addEventListener("pointerdown",e=>{
      drag=[e.clientX,e.clientY];canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener("pointermove",e=>{
      if(!drag)return;
      yaw+=(e.clientX-drag[0])*.012;pitch=Math.max(-1.45,Math.min(1.45,
         pitch+(e.clientY-drag[1])*.012));
      drag=[e.clientX,e.clientY];draw();
    });
    canvas.addEventListener("pointerup",()=>drag=null);
    canvas.addEventListener("pointercancel",()=>drag=null);
    canvas.addEventListener("wheel",e=>{
      e.preventDefault();
      zoom=Math.max(.2,Math.min(7,zoom*(e.deltaY<0?1.13:.89)));draw();
    },{passive:false});
    async function selectSources(files,category){
      const epoch=++serial;
      kind=category; model=null;tint=[170,184,200];drag=null;zoom=1;
      for(const u of urls)URL.revokeObjectURL(u);urls=[];
      thumbs.replaceChildren();
      const chosen=Array.from(files||[]);
      const extension=category==="Track"?".lev":".obj";
      const first=chosen.find(f=>f.name.toLowerCase().endsWith(extension));
      try{
        if(first){
          model=extension===".lev"?parseLev(await first.arrayBuffer()):
                                      parseObj(await first.text());
          if(epoch!==serial)return;
          fit();
          status.textContent=model.kind+" / "+model.vertices.length+
              " vertices / "+model.faces.length+
              " preview triangles. Drag to rotate, scroll to zoom.";
        }else{
          status.textContent="Choose a native "+extension.toUpperCase()+
             " file to see its geometry.";
        }
      }catch(error){
        if(epoch!==serial)return;
        model=null;status.textContent="Preview unavailable: "+
                                    String(error.message||error);
      }
      for(const file of chosen.filter(f=>/\.(png|bmp|jpg|jpeg|webp)$/i.test(f.name)).slice(0,40)){
        const item=document.createElement("button");
        item.type="button";item.className="forge-texture-item";
        const url=URL.createObjectURL(file);urls.push(url);
        const img=document.createElement("img");img.src=url;img.alt="";
        img.width=48;img.height=48;
        const label=document.createElement("span");label.textContent=file.name;
        item.append(img,label);thumbs.append(item);
        item.addEventListener("click",()=>{
          const off=document.createElement("canvas");off.width=off.height=4;
          const cc=off.getContext("2d");
          try{
            cc.drawImage(img,0,0,4,4);
            const pixels=cc.getImageData(0,0,4,4).data;const sum=[0,0,0];
            for(let k=0;k<16;k++)for(let j=0;j<3;j++)sum[j]+=pixels[k*4+j];
            tint=sum.map(v=>v/16);
            draw();
          }catch(_){/* unreadable image cannot influence the native ZIP */}
        });
      }
      if(epoch===serial)draw();
    }
    draw();
    return {selectSources};
  }
  window.ForgeModVisualizer=Object.freeze({parseObj,parseLev,browserPreview});
})();
