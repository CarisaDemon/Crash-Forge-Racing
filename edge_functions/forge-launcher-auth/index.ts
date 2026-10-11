import { createClient } from "npm:@supabase/supabase-js@2.50.0";

// Desktop/browser identity bridge. Anon endpoints are verifier- and rate-limited;
// browser approval requires a real Supabase user JWT. No service key leaves Edge.
const PROJECT="mjvpkerobjgoldmimyxz.supabase.co";
const ORIGIN="https://carisademon.github.io";
const WEB="https://carisademon.github.io/Crash-Forge-Racing/forge-hub/account.html";
const HEX=/^[a-f0-9]{64}$/;
const UUID=/^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/i;
const NAME=/^[a-z][a-z0-9_]{2,23}$/;
const ABC="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const HEADERS={
  "Access-Control-Allow-Origin":ORIGIN,
  "Access-Control-Allow-Headers":"apikey, authorization, content-type, x-client-info",
  "Access-Control-Allow-Methods":"POST, OPTIONS",
  "Access-Control-Max-Age":"600",
  "Cache-Control":"no-store",
  "Referrer-Policy":"no-referrer",
  "Vary":"Origin"
};
const reply=(data:Record<string,unknown>,status=200)=>new Response(JSON.stringify(data),{
  status,headers:{...HEADERS,"Content-Type":"application/json; charset=utf-8"}
});
function setting(mapName:string,oldName:string):string {
  const old=Deno.env.get(oldName);if(old)return old;
  try {
    const all=JSON.parse(Deno.env.get(mapName)||"{}");
    const value=all.default||Object.values(all)[0];
    return typeof value==="string"?value:typeof value?.key==="string"?value.key:"";
  } catch {return "";}
}
function credentials() {
  const url=Deno.env.get("SUPABASE_URL")||"";
  if(new URL(url).hostname!==PROJECT)throw new Error("Bad project");
  const pub=setting("SUPABASE_PUBLISHABLE_KEYS","SUPABASE_ANON_KEY");
  const secret=setting("SUPABASE_SECRET_KEYS","SUPABASE_SERVICE_ROLE_KEY");
  if(!pub||!secret)throw new Error("Unavailable keys");
  return {url,pub,secret};
}
async function sha(text:string) {
  const digest=await crypto.subtle.digest("SHA-256",new TextEncoder().encode(text));
  return Array.from(new Uint8Array(digest),v=>v.toString(16).padStart(2,"0")).join("");
}
const code=()=>Array.from(crypto.getRandomValues(new Uint8Array(8)),
 x=>ABC[x%ABC.length]).join("");
function bearer(req:Request) {
  const raw=req.headers.get("authorization")||"";
  const match=raw.match(/^Bearer\s+([A-Za-z0-9._-]+)$/i);
  if(!match||match[1].startsWith("sb_"))return "";
  return match[1];
}
async function authenticated(admin:any,req:Request) {
  const token=bearer(req);if(!token)return null;
  const {data,error}=await admin.auth.getUser(token);
  return error?null:(data?.user||null);
}
async function throttle(admin:any,key:string,cap:number,seconds:number,secret:string) {
  const bucket=await sha(secret+"|"+key);
  const {data,error}=await admin.rpc("forge_launcher_throttle",{
    p_bucket_hash:bucket,p_max_count:cap,p_window_seconds:seconds
  });
  if(error)throw new Error("Rate limiter unavailable");
  return data===true;
}
function remoteIp(req:Request) {
  return String(req.headers.get("cf-connecting-ip")||
    req.headers.get("x-real-ip")||
    req.headers.get("x-forwarded-for")?.split(",")[0]||"unknown").slice(0,80);
}
async function usernameLogin(req:Request,body:any,admin:any,pubClient:any,secret:string) {
  const name=String(body.identifier||"").trim().toLowerCase();
  const password=body.password;
  if(!NAME.test(name)||typeof password!=="string"||!password||
     password.length>256)return reply({error:"Invalid username or password"},401);
  const ip=remoteIp(req);
  const allowed=(await throttle(admin,"login-ip:"+ip,70,600,secret))&&
    (await throttle(admin,"login-user:"+ip+":"+name,7,600,secret));
  if(!allowed)return reply({error:"Too many attempts. Try again later."},429);
  const found=await admin.from("forge_accounts")
    .select("user_id").eq("username",name).maybeSingle();
  if(found.error)throw new Error("Lookup failed");
  if(!found.data)return reply({error:"Invalid username or password"},401);
  const account=await admin.auth.admin.getUserById(found.data.user_id);
  const email=account?.data?.user?.email;
  if(account.error||!email)return reply({error:"Invalid username or password"},401);
  const result=await pubClient.auth.signInWithPassword({email,password});
  if(result.error||!result.data.session ||
     result.data.user?.id!==found.data.user_id)
    return reply({error:"Invalid username or password"},401);
  const session=result.data.session;
  return reply({access_token:session.access_token,
    refresh_token:session.refresh_token,
    expires_in:session.expires_in,token_type:"bearer",
    user_id:result.data.user.id});
}
async function pairStart(req:Request,body:any,admin:any,secret:string) {
  const challenge=String(body.challenge||"").toLowerCase();
  const mode=String(body.mode||"github");
  if(!HEX.test(challenge)||!["github","connect","link"].includes(mode))
    return reply({error:"Invalid desktop authorization request"},400);
  const ip=remoteIp(req);
  if(!await throttle(admin,"pair-start:"+ip,18,600,secret))
    return reply({error:"Too many connection attempts"},429);
  let bound_user_id:string|null=null;
  if(req.headers.has("authorization")) {
    const user=await authenticated(admin,req);
    if(!user)return reply({error:"Please sign in again"},401);
    bound_user_id=user.id;
  }
  if(mode==="link"&&!bound_user_id)
    return reply({error:"Sign in before linking a provider"},401);
  const display_code=code();
  const record=await admin.from("forge_launcher_pairings")
    .insert({challenge,display_code,bound_user_id}).select("id,expires_at").single();
  if(record.error)throw new Error("Cannot create request");
  const url=new URL(WEB);
  url.searchParams.set("pair",record.data.id);
  url.searchParams.set("code",display_code);
  url.searchParams.set("mode",mode);
  return reply({id:record.data.id,display_code,
    expires_at:record.data.expires_at,url:url.toString()});
}
async function pairApprove(req:Request,body:any,admin:any) {
  const user=await authenticated(admin,req);
  if(!user)return reply({error:"Sign in to Forge Hub first"},401);
  const id=String(body.id||""), display_code=String(body.code||"");
  if(!UUID.test(id)|| !/^[A-HJ-NP-Z2-9]{8}$/.test(display_code))
    return reply({error:"Invalid pairing request"},400);
  const request=await admin.from("forge_launcher_pairings")
    .select("id,bound_user_id,state").eq("id",id).eq("display_code",display_code)
    .eq("state","pending").gt("expires_at",new Date().toISOString()).maybeSingle();
  if(request.error||!request.data)return reply({error:"Request expired or invalid"},409);
  if(request.data.bound_user_id && request.data.bound_user_id!==user.id)
    return reply({error:"This launcher is connected to another account. Sign in to that same Forge Hub account."},403);
  const updated=await admin.from("forge_launcher_pairings")
    .update({state:"approved",approved_user_id:user.id})
    .eq("id",id).eq("state","pending").eq("display_code",display_code)
    .gt("expires_at",new Date().toISOString()).select("id").maybeSingle();
  if(updated.error||!updated.data)return reply({error:"Request could not be approved"},409);
  return reply({approved:true,message:"This browser has authorized your launcher."});
}
async function pairClaim(body:any,admin:any) {
  const id=String(body.id||""),verifier=String(body.verifier||"");
  if(!UUID.test(id)||!HEX.test(verifier))
    return reply({error:"Invalid desktop verifier"},400);
  const challenge=await sha(verifier);
  const found=await admin.from("forge_launcher_pairings")
    .select("state,approved_user_id")
    .eq("id",id).eq("challenge",challenge)
    .gt("expires_at",new Date().toISOString()).maybeSingle();
  if(found.error||!found.data)
    return reply({error:"Authorization expired. Start again."},410);
  if(found.data.state==="pending")return reply({status:"pending"});
  if(found.data.state!=="approved"||!found.data.approved_user_id)
    return reply({error:"Authorization already used"},410);
  const claimed=await admin.from("forge_launcher_pairings")
    .update({state:"claimed"}).eq("id",id).eq("challenge",challenge)
    .eq("state","approved").gt("expires_at",new Date().toISOString())
    .select("approved_user_id").maybeSingle();
  if(claimed.error||!claimed.data)return reply({error:"Authorization already used"},410);
  const authUser=await admin.auth.admin.getUserById(claimed.data.approved_user_id);
  const email=authUser?.data?.user?.email;
  if(authUser.error||!email)
    return reply({error:"Your Forge Hub account needs a confirmed email address"},409);
  // Admin generates a single-use OTP but never returns it to the web page.
  // Only the requesting launcher, proving its 256-bit verifier, can claim it.
  const grant=await admin.auth.admin.generateLink({
    type:"magiclink",email
  });
  const token_hash=grant?.data?.properties?.hashed_token;
  if(grant.error||typeof token_hash!=="string"||!token_hash)
    return reply({error:"Could not complete the authorized login"},503);
  return reply({status:"approved",token_hash,verify_type:"magiclink"});
}
Deno.serve(async req=>{
  if(req.method==="OPTIONS")return new Response(null,{status:204,headers:HEADERS});
  if(req.method!=="POST")return reply({error:"Method not allowed"},405);
  const origin=req.headers.get("origin");
  if(origin && origin!==ORIGIN)return reply({error:"Forbidden origin"},403);
  try {
    const raw=await req.text();
    if(raw.length>4096)return reply({error:"Request too large"},413);
    let body:any;try {body=JSON.parse(raw);}catch{return reply({error:"Invalid JSON"},400);}
    if(!body||typeof body!=="object"||Array.isArray(body))return reply({error:"Invalid request"},400);
    const cfg=credentials();
    const admin=createClient(cfg.url,cfg.secret,
      {auth:{persistSession:false,autoRefreshToken:false}});
    const pub=createClient(cfg.url,cfg.pub,
      {auth:{persistSession:false,autoRefreshToken:false}});
    switch(String(body.action||"")) {
      case "username_login": return await usernameLogin(req,body,admin,pub,cfg.secret);
      case "pair_start": return await pairStart(req,body,admin,cfg.secret);
      case "pair_approve": return await pairApprove(req,body,admin);
      case "pair_claim": return await pairClaim(body,admin);
      default:return reply({error:"Unknown action"},400);
    }
  }catch(_error) {
    console.error("Forge Launcher authentication backend failed");
    return reply({error:"Forge Hub authentication service is unavailable"},503);
  }
});