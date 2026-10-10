import { createClient } from "npm:@supabase/supabase-js@2.50.0";

// GET: public verified catalog. POST: OAuth-authenticated GitHub moderator only.
// Secret/service-role API keys stay inside Supabase Edge Functions.
const ORIGIN="https://carisademon.github.io";
const PROJECT="mjvpkerobjgoldmimyxz.supabase.co";
const MAX_ZIP=50*1024*1024;
const UUID=/^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/i;
const SHA=/^[a-f0-9]{64}$/;
const PUBLIC_RIGHTS_LABEL="Community upload - rights unverified";
const EXT=new Set([".obj",".mtl",".png",".jpg",".jpeg",".bmp",".tga",".ini",
 ".cfg",".json",".txt",".md",".wav",".ogg",".cseq",".bin",".dds",
 ".webp",".gif",".xml",".dat",".gltf",".glb",".lev",".vrm"]);
const HEADERS={
 "Access-Control-Allow-Origin":ORIGIN,
 "Access-Control-Allow-Methods":"GET, POST, OPTIONS",
 "Access-Control-Allow-Headers":"apikey, authorization, x-client-info, content-type",
 "Access-Control-Max-Age":"600",
 "Vary":"Origin"
};
const send=(data,status=200)=>new Response(JSON.stringify(data),{
 status,headers:{...HEADERS,"Content-Type":"application/json; charset=utf-8",
 "Cache-Control":"no-store"}
});
const brief=e=>String(e?.message||e||"Backend error").slice(0,180);

function lookupKey(dictionary,legacy) {
 const original=Deno.env.get(legacy);
 if(original) return original;
 let values;
 try{values=JSON.parse(Deno.env.get(dictionary)||"{}");}catch{return "";}
 const v=values && typeof values==="object" ? (values.default||Object.values(values)[0]):"";
 return typeof v==="string" ? v : (typeof v?.key==="string" ? v.key : "");
}
function getCredentials() {
 const url=Deno.env.get("SUPABASE_URL")||"";
 if(!url||new URL(url).hostname!==PROJECT)throw Error("Unexpected Supabase URL");
 const pub=lookupKey("SUPABASE_PUBLISHABLE_KEYS","SUPABASE_ANON_KEY");
 const secret=lookupKey("SUPABASE_SECRET_KEYS","SUPABASE_SERVICE_ROLE_KEY");
 if(!pub||!secret)throw Error("Supabase Edge credentials unavailable");
 return {url,pub,secret};
}
function inspectZip(buffer) {
 // Inspect the ZIP central directory. No extraction or script execution.
 const bytes=new Uint8Array(buffer);
 if(bytes.length<22||bytes[0]!==80||bytes[1]!==75||bytes[2]!==3||bytes[3]!==4)
  throw Error("Not a ZIP archive");
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 let end=-1;
 for(let i=bytes.length-22;i>=Math.max(0,bytes.length-65557);--i)
  if(v.getUint32(i,true)===0x06054b50 &&
    i+22+v.getUint16(i+20,true)===bytes.length){end=i;break;}
 if(end<0)throw Error("Invalid ZIP directory");
 const count=v.getUint16(end+10,true), size=v.getUint32(end+12,true),
  start=v.getUint32(end+16,true);
 if(!count||count>2000||count===65535||size===0xffffffff||start===0xffffffff||
    start+size>end||v.getUint16(end+4,true)!==0||v.getUint16(end+6,true)!==0||
    v.getUint16(end+8,true)!==count)throw Error("Unsupported ZIP directory");
 const decoder=new TextDecoder("utf-8",{fatal:true});
 let offset=start,total=0,files=0;
 const seen=new Set();
 for(let i=0;i<count;i++) {
  if(offset+46>start+size||v.getUint32(offset,true)!==0x02014b50)
   throw Error("Invalid ZIP entry header");
  const flags=v.getUint16(offset+8,true),method=v.getUint16(offset+10,true),
   unpacked=v.getUint32(offset+24,true),
   namelen=v.getUint16(offset+28,true),extra=v.getUint16(offset+30,true),
   comment=v.getUint16(offset+32,true),
   packed=v.getUint32(offset+20,true),
   local=v.getUint32(offset+42,true),endpos=offset+46+namelen+extra+comment;
  if(!namelen||endpos>start+size||(flags&1)||![0,8].includes(method)||
    unpacked===0xffffffff||packed===0xffffffff||
    local+30>start||v.getUint32(local,true)!==0x04034b50||
    (((v.getUint32(offset+38,true)>>>16)&0o170000)===0o120000))
   throw Error("Invalid ZIP entry, encryption or symlink");
  const name=decoder.decode(bytes.subarray(offset+46,offset+46+namelen));
  const segments=name.split("/");
  if(name.startsWith("/")||name.includes("\\")||/^[A-Za-z]:/.test(name)||
     /[\x00-\x1f\x7f]/.test(name)||seen.has(name)||
     segments.some((part,index)=>part==="."||part===".."||
       (part===""&&index!==segments.length-1)) ||
     (segments[segments.length-1]===""&&!name.endsWith("/")))
   throw Error("Unsafe ZIP path");
  seen.add(name);
  if(!name.endsWith("/")){
   const base=segments[segments.length-1],dot=base.lastIndexOf(".");
   const extension=dot<0?"":base.slice(dot).toLowerCase();
   if(!EXT.has(extension))throw Error("Disallowed file type: "+extension);
   total+=unpacked; files++;
   if(total>1024*1024*1024)throw Error("ZIP expands over 1 GiB");
  }
  offset=endpos;
 }
 if(offset!==start+size||!files)throw Error("Invalid/empty ZIP");
 return {files,expandedBytes:total};
}
async function hash(buffer){
 const bytes=new Uint8Array(await crypto.subtle.digest("SHA-256",buffer));
 return Array.from(bytes,b=>b.toString(16).padStart(2,"0")).join("");
}
async function catalog(admin) {
 const {data,error}=await admin.from("forge_public_mods").select(
 "id,title,author,author_github_login,author_github_id,type,compatibility,"+
 "version,description,map_kind,racer_class,kart_drive,wheel_setup,"+
 "download_url,page_url,sha256,license,published_at")
 .order("published_at",{ascending:false}).limit(300);
 if(error)throw error;
 const items=data||[];
 return send({
  schema_version:1,
  updated_at:items.length?String(items[0].published_at).slice(0,10):"2026-10-09",
  game:{},
  news:[{
   category:"COMMUNITY",
   title:"Forge Hub accepts published community creations",
   date:"2026-10-09",
   description:"Private review and public publication are separate. Only authorized releases appear in the catalog.",
   url:"https://carisademon.github.io/Crash-Forge-Racing/forge-hub/"
  }],
  mods:items.map(x=>({
   id:x.id,title:x.title,author:x.author,
   author_github_login:x.author_github_login,
   author_github_id:x.author_github_id,
   type:x.type,compatibility:"cfr",version:x.version,
   description:x.description,map_kind:x.map_kind,
   racer_class:x.racer_class,kart_drive:x.kart_drive,
   wheel_setup:x.wheel_setup,download_url:x.download_url,
   page_url:x.page_url,sha256:x.sha256,license:x.license,
   created_at:x.published_at,
  }))
 });
}
async function publish(request,admin,cfg){
 const authorization=request.headers.get("authorization")||"";
 const match=authorization.match(/^Bearer\s+(.+)$/i);
 if(!match||match[1].startsWith("sb_"))
  return send({error:"GitHub sign-in required"},401);
 const {data,error}=await admin.auth.getUser(match[1]);
 if(error||!data?.user)return send({error:"Session expired. Sign in again."},401);
 const user=data.user;
 const client=createClient(cfg.url,cfg.pub,{
  auth:{autoRefreshToken:false,persistSession:false},
  global:{headers:{Authorization:authorization}}
 });
 const access=await client.rpc("forge_is_moderator");
 if(access.error||access.data!==true)
  return send({error:"Verified moderator permission required for public release"},403);
 const raw=await request.text();
 if(raw.length>6000)return send({error:"Request too large"},413);
 let req;
 try{req=JSON.parse(raw);}catch{return send({error:"Invalid JSON"},400);}
 const id=String(req?.submission_id||"").trim();
 if(!UUID.test(id))return send({error:"Invalid submission ID"},400);
 const action=String(req?.action||"publish").trim();
 if(action==="unpublish"){
  // OAuth-verified moderators (including the owner) may withdraw public
  // releases. The trusted SQL function rechecks membership and audits
  // moderator ID plus the mandatory takedown reason.
  const reason=String(req?.reason||"").trim();
  if(reason.length<8||reason.length>500)
   return send({error:"A takedown reason (8-500 characters) is required"},400);
  const existing=await admin.from("forge_public_mods").select("id")
    .eq("submission_id",id).maybeSingle();
  if(existing.error)throw existing.error;
  if(!existing.data)return send({error:"This mod is not published"},404);
  const path="mods/"+id+".zip";
  const removed=await admin.storage.from("forge-public-mods").remove([path]);
  if(removed.error)return send({error:"Public ZIP removal failed: "+brief(removed.error)},409);
  const result=await admin.rpc("forge_remove_publication",{
   p_submission_id:id,p_reason:reason,p_moderator_id:user.id
  });
  if(result.error)return send({error:"Download removed, but listing removal must be retried: "+brief(result.error)},409);
  return send({removed:true,message:"Public ZIP and listing removed; private review remains intact"});
 }
 if(action==="delete_submission"){
  const reason=String(req?.reason||"").trim();
  if(reason.length<8||reason.length>500)
   return send({error:"Deletion reason must be 8-500 characters"},400);
  // The trusted SQL function independently checks verified GitHub moderation.
  // The public listing must already be withdrawn before permanent deletion.
  const marked=await admin.rpc("forge_mark_submission_deleted",{
   p_submission_id:id,p_reason:reason,p_moderator_id:user.id
  });
  if(marked.error)
   return send({error:"Cannot delete mod: "+brief(marked.error)},409);
  const privatePath=String(marked.data||"");
  if(!/^[a-f0-9-]{36}\/[a-f0-9-]{36}\.zip$/.test(privatePath))
   return send({error:"Deletion recorded, but unsafe ZIP path requires review"},409);
  const cleanup=await admin.storage.from("forge-mod-queue").remove([privatePath]);
  if(cleanup.error)
   return send({error:"Deletion recorded; private ZIP cleanup failed: "+
    brief(cleanup.error)+". Retry ZIP removal from Moderation.",deletion_recorded:true},409);
  const confirmed=await admin.rpc("forge_complete_submission_deletion",{
   p_submission_id:id,p_moderator_id:user.id
  });
  if(confirmed.error)
   return send({error:"ZIP removed; confirmation failed: "+
    brief(confirmed.error)+". Retry removal from Moderation.",deletion_recorded:true},409);
  return send({deleted:true,zip_removed:true,creator_notified:true,
   message:"Mod ZIP deleted. Creator will see the reason until they dismiss it."});
 }
 if(action!=="publish")return send({error:"Unknown publication action"},400);
 // Never assign a permission claim or Creative Commons license
 // automatically. The public catalog explicitly marks unverified rights.
 const license=PUBLIC_RIGHTS_LABEL;
 const moderatorNotes=String(req?.moderator_notes||"").trim();
 const basis=moderatorNotes; // Optional explanation; authorization still requires moderator and explicit consent.
 if(moderatorNotes.length>1000||req?.publish_confirmed!==true)
  return send({error:"Confirm reviewed redistribution rights and limit optional notes to 1000 characters"},400);
 const {data:sub,error:subErr}=await admin.from("mod_submissions")
  .select("id,owner_id,status,publication_blocked,zip_path,sha256,file_size_bytes,description,replaces_submission_id")
  .eq("id",id).maybeSingle();
 if(subErr||!sub)return send({error:"Submission not found"},404);
 if(sub.status!=="approved")return send({error:"Approve before publishing"},409);
 if(sub.publication_blocked)
  return send({error:"Publication locked by moderation. Unlock it before publishing."},403);
 if(!SHA.test(String(sub.sha256||""))||Number(sub.file_size_bytes)<=0||
   Number(sub.file_size_bytes)>MAX_ZIP||
   !String(sub.zip_path).startsWith(String(sub.owner_id)+"/"))
  return send({error:"Invalid reviewed ZIP metadata"},422);
 const check=await admin.from("forge_public_mods").select("id")
  .eq("submission_id",id).maybeSingle();
 if(check.error)throw check.error;
 if(check.data)return send({error:"Already published",public_id:check.data.id},409);
 // Preserve public ZIP until an approved revision's listing is atomically
 // replaced. The server never trusts the client's proposed previous mod.
 let previousPublic=null;
 if(sub.replaces_submission_id) {
  const prior=await admin.from("forge_public_mods")
   .select("id,submission_id,author_github_id,type,title")
   .eq("submission_id",sub.replaces_submission_id).maybeSingle();
  if(prior.error)throw prior.error;
  previousPublic=prior.data;
 }
 const download=await admin.storage.from("forge-mod-queue").download(sub.zip_path);
 if(download.error||!download.data)throw Error("Private ZIP unavailable");
 if(download.data.size!==Number(sub.file_size_bytes))
  return send({error:"ZIP size mismatch"},422);
 const bytes=await download.data.arrayBuffer();
 if(await hash(bytes)!==sub.sha256)
  return send({error:"SHA-256 mismatch; publication refused"},422);
 try{inspectZip(bytes);}catch(e){
  return send({error:"ZIP rejected: "+brief(e)},422);
 }
 const path="mods/"+id+".zip";
 const copy=await admin.storage.from("forge-mod-queue").copy(
  sub.zip_path,path,{destinationBucket:"forge-public-mods"});
 if(copy.error)return send({error:"Public ZIP copy failed: "+brief(copy.error)},409);
 // SQL atomically swaps the public listing to the reviewed revision,
 // retaining the same ID. Old ZIP stays live until promotion succeeds.
 const finalized=await admin.rpc("forge_finalize_publication",{
  p_submission_id:id,p_license:license,
  p_rights_basis:basis,p_moderator_id:user.id
 });
 if(finalized.error){
  await admin.storage.from("forge-public-mods").remove([path]);
  return send({error:"Final publication failed: "+brief(finalized.error)},409);
 }
 let archiveCleanupPending=false;
 if(previousPublic) {
  // After the database promotion, retire the prior public file.
  const oldPath="mods/"+previousPublic.submission_id+".zip";
  const retired=await admin.storage.from("forge-public-mods").remove([oldPath]);
  if(retired.error) {
   archiveCleanupPending=true;
   console.error("Previous public ZIP cleanup pending:",brief(retired.error));
  }
 }
 return send({published:true,public_id:finalized.data,
   replaced:!!previousPublic,archive_cleanup_pending:archiveCleanupPending,
   message:archiveCleanupPending ?
    "New version is live; previous ZIP cleanup needs moderator attention" :
    "Approved mod version is now live in the original Forge Hub listing"},201);
}
Deno.serve(async request=>{
 if(request.method==="OPTIONS")
  return new Response(null,{status:204,headers:HEADERS});
 try{
  if(!["GET","POST"].includes(request.method))
   return send({error:"Method not allowed"},405);
  const cfg=getCredentials();
  const admin=createClient(cfg.url,cfg.secret,{
   auth:{autoRefreshToken:false,persistSession:false}
  });
  if(request.method==="GET")return await catalog(admin);
  return await publish(request,admin,cfg);
 }catch(e){
  console.error("Forge Hub publication failure:",brief(e));
  return send({error:"Forge Hub backend unavailable; no mod was published"},503);
 }
});
