/* Forge Hub desktop login bridge; never sends auth tokens through URL parameters. */
"use strict";
const get = id => document.getElementById(id);
const safeMessage = error => String(error?.message || error?.error || error ||
  "Account service unavailable").slice(0,180);
const BASE = "https://mjvpkerobjgoldmimyxz.supabase.co";
const AUTH = BASE + "/functions/v1/forge-launcher-auth";
const USERNAME = /^[a-z][a-z0-9_]{2,23}$/;
const RESERVED = new Set(["admin","moderator","support","official","system",
  "forge_hub","crash_forge","carisademon","github","supabase"]);
const UUID = /^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/i;
const CODE = /^[A-HJ-NP-Z2-9]{8}$/;
const query = new URLSearchParams(location.search);
const pairId = query.get("pair") || "";
const pairCode = query.get("code") || "";
const pairMode = query.get("mode") || "connect";
const validPair = UUID.test(pairId) && CODE.test(pairCode) &&
  ["github","connect","link"].includes(pairMode);
let client=null, publicKey="", activeSession=null;
let busy=false;
const status=(text,good=false)=>{
  const banner=get("accountStatus");
  banner.textContent=text;banner.style.borderLeftColor=good?"#55cc85":"#ff760c";
};
function pendingReturn(){
  const url = new URL("./account.html", location.href);
  if(validPair){
    url.searchParams.set("pair",pairId);
    url.searchParams.set("code",pairCode);
    url.searchParams.set("mode",pairMode);
  }
  // Only a relative, validated Forge Hub account path is stored.
  return url.pathname + url.search;
}
async function api(payload,accessToken=""){
  const headers={"apikey":publicKey,"Content-Type":"application/json"};
  if(accessToken) headers.Authorization="Bearer "+accessToken;
  const response=await fetch(AUTH,{
    method:"POST",cache:"no-store",credentials:"omit",
    headers,body:JSON.stringify(payload)
  });
  const result=await response.json().catch(()=>({}));
  if(!response.ok)throw Error(safeMessage(result.error||"Request rejected"));
  return result;
}
async function verifiedBrowserSession(){
  // getSession() only reads the cached JWT from storage. A signed-out
  // browser can still display a valid-looking profile while GoTrue rejects
  // that revoked session_id with 403 session_not_found.
  const {data,error}=await client.auth.getSession();
  let session=data?.session || null;
  if(error||!session)throw Error("Your Forge Hub session is no longer active. Sign in again.");
  const proof=await client.auth.getUser(session.access_token);
  if(!proof.error && proof.data?.user?.id===session.user?.id)return session;
  // A verified user is mandatory before sending a one-time pairing approval.
  // Attempt one local refresh; no password, token or OTP goes in the URL.
  const renewed=await client.auth.refreshSession();
  session=renewed.data?.session || null;
  if(renewed.error||!session)
    throw Error("This browser session was revoked. Sign in with GitHub or email again.");
  const confirmation=await client.auth.getUser(session.access_token);
  if(confirmation.error || confirmation.data?.user?.id!==session.user?.id)
    throw Error("Forge Hub couldn't verify this session. Sign in again.");
  return session;
}

async function displayVerifiedSession(session){
  if(!session){await refreshProfile(null);return;}
  try {
    await refreshProfile(await verifiedBrowserSession());
  } catch(error) {
    // Do not mistake a locally cached user object for active server login.
    await refreshProfile(null);
    status("Your previous Forge Hub session is no longer valid. Sign in again to connect the launcher.");
  }
}
async function refreshProfile(session){
  activeSession=session;
  const signed=!!(session&&session.user);
  get("loginBox").classList.toggle("hidden",signed);
  get("registrationBox").classList.toggle("hidden",signed);
  get("profileBox").classList.toggle("hidden",!signed);
  get("approve").disabled=!signed;
  if(!signed){
    get("profileName").textContent="";
    get("profileGitHub").textContent="";
    return;
  }
  const user=session.user;
  const uid=String(user.id||"");
  let profile=null;
  try{
    const {data,error}=await client.from("forge_accounts")
      .select("username,display_name").eq("user_id",uid).maybeSingle();
    if(error)throw error;
    profile=data;
  }catch(error){
    status("Account profile couldn't be read: "+safeMessage(error));
  }
  // A registration may require email confirmation before the username can
  // be claimed. This is the first authenticated step after confirmation.
  const suggested=String(user.user_metadata?.forge_username_request||"").toLowerCase();
  if(!profile && USERNAME.test(suggested) && !RESERVED.has(suggested)){
    try{
      const {error}=await client.from("forge_accounts").insert({
        user_id:uid,username:suggested,display_name:suggested
      });
      if(!error)profile={username:suggested,display_name:suggested};
    }catch{/* username collision: the user may choose a different one */}
  }
  const ids=Array.isArray(user.identities)?user.identities:[];
  const github=ids.some(item=>item.provider==="github");
  get("profileName").textContent=profile?.display_name ||
    user.user_metadata?.full_name || "Forge Hub member";
  get("profileEmail").textContent=String(user.email||"Email not supplied by identity provider");
  get("profileGitHub").textContent=github?
    "GitHub identity: linked to this Forge Hub account.":
    "GitHub identity: not yet linked.";
  get("editUsername").value=profile?.username||"";
  get("linkGitHub").disabled=github;
  if(validPair)status("Signed in. Confirm the same code on your Windows launcher before authorizing.",true);
  else status("Signed in to your Forge Hub account.",true);
}
async function continueGitHub(linkIdentity=false){
  if(!client || busy)return;
  busy=true;
  try{
    sessionStorage.setItem("cfr_account_return",pendingReturn());
    // The existing studio.html is the project's already-configured GitHub
    // OAuth callback. We return to account.html after the SDK recovers session.
    const redirectTo=new URL("./studio.html",location.href).href;
    let result;
    if(linkIdentity){
      if(!activeSession)throw Error("Sign in to an existing account first.");
      result=await client.auth.linkIdentity({
        provider:"github",options:{redirectTo}
      });
    }else{
      result=await client.auth.signInWithOAuth({
        provider:"github",options:{redirectTo}
      });
    }
    if(result.error)throw result.error;
    status("Opening GitHub's official authorization page...");
  }catch(error){status("GitHub connection unavailable: "+safeMessage(error));}
  finally{busy=false;}
}
async function signIn(event){
  event.preventDefault();
  if(!client||busy)return;
  const identifier=get("loginId").value.trim();
  const password=get("loginPassword").value;
  get("loginPassword").value="";
  if(!password){status("Enter your password.");return;}
  busy=true;
  try{
    if(identifier.includes("@")){
      const {data,error}=await client.auth.signInWithPassword({
        email:identifier,password
      });
      if(error||!data.session)throw error||Error("Sign-in failed");
    }else{
      const result=await api({
        action:"username_login",identifier:identifier.toLowerCase(),password
      });
      const {error}=await client.auth.setSession({
        access_token:result.access_token,
        refresh_token:result.refresh_token
      });
      if(error)throw error;
    }
    const {data}=await client.auth.getSession();
    await refreshProfile(data.session);
  }catch(error){
    status("Sign-in failed. Check credentials or email confirmation.");
  }finally{busy=false;}
}
async function register(event){
  event.preventDefault();
  if(!client||busy)return;
  const username=get("registerName").value.trim().toLowerCase();
  const email=get("registerEmail").value.trim();
  const password=get("registerPassword").value;
  get("registerPassword").value="";
  if(!USERNAME.test(username)||RESERVED.has(username)){
    status("Choose a username of 3-24 letters, digits or underscores.");return;
  }
  if(password.length<8){status("Password must be at least eight characters.");return;}
  busy=true;
  try{
    const {data,error}=await client.auth.signUp({
      email,password,options:{
        emailRedirectTo:new URL("./studio.html",location.href).href,
        data:{forge_username_request:username}
      }
    });
    if(error)throw error;
    if(data.session){
      await refreshProfile(data.session);
      status("Account created. Your session is active.",true);
    }else{
      status("Check your email for confirmation, then sign in. Your chosen username will be requested after verification.",true);
    }
  }catch(error){status("Could not create account: "+safeMessage(error));}
  finally{busy=false;}
}
async function saveUsername(){
  if(!client||!activeSession||busy)return;
  const username=get("editUsername").value.trim().toLowerCase();
  if(!USERNAME.test(username)||RESERVED.has(username)){
    status("Invalid or reserved username.");return;
  }
  busy=true;
  try{
    const uid=activeSession.user.id;
    const payload={user_id:uid,username,display_name:username};
    const existing=await client.from("forge_accounts")
      .select("user_id").eq("user_id",uid).maybeSingle();
    if(existing.error)throw existing.error;
    const result=existing.data?
      await client.from("forge_accounts").update({
        username,display_name:username
      }).eq("user_id",uid):
      await client.from("forge_accounts").insert(payload);
    if(result.error)throw result.error;
    const {data}=await client.auth.getSession();
    await refreshProfile(data.session);
    status("Username saved. Forge Hub and Launcher will use the same account.",true);
  }catch(error){
    status("Username not available. Try another unique name.");
  }finally{busy=false;}
}
async function approve(){
  if(!validPair||!client||busy)return;
  busy=true;
  try{
    const session=await verifiedBrowserSession();
    // No silent auto-approval: this request occurs only on the user's click.
    // Never authorize from a revoked JWT merely because its saved expiry
    // has not elapsed.
    await api({action:"pair_approve",id:pairId,code:pairCode},
              session.access_token);
    get("approve").disabled=true;
    status("Launcher authorized! Return to Crash Forge; it will connect automatically.",true);
  }catch(error){
    const message=safeMessage(error);
    if(/session|sign in/i.test(message)){
      await refreshProfile(null);
      status("Could not authorize launcher: "+message+
        " Use CONTINUE WITH GITHUB or sign in again, then reopen the launcher link.");
    }else{
      status("Could not authorize launcher: "+message);
    }
  }finally{busy=false;}
}
async function setup(){
  if(validPair){
    get("pairBox").classList.remove("hidden");
    get("pairCode").textContent=pairCode;
    status("Check that this code exactly matches your launcher before continuing.");
  }else if(pairId||pairCode){
    status("Invalid launcher connection code; reopen the login from the launcher.");
  }
  try{
    const response=await fetch("./creator-config.json",{cache:"no-store"});
    if(!response.ok)throw Error("Account config unavailable");
    const cfg=await response.json();
    const url=new URL(cfg.supabase_url);
    publicKey=String(cfg.supabase_publishable_key||"");
    if(url.origin!==BASE || !publicKey.startsWith("sb_publishable_") ||
       publicKey.startsWith("sb_secret_")){
      throw Error("Invalid public account configuration");
    }
    const sdk=await import("https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.50.0/+esm");
    client=sdk.createClient(BASE,publicKey,{
      auth:{flowType:"pkce",detectSessionInUrl:true,persistSession:true}
    });
    const {data,error}=await client.auth.getSession();
    if(error)throw error;
    await displayVerifiedSession(data.session);
    client.auth.onAuthStateChange((_event,session)=>{
      window.setTimeout(()=>displayVerifiedSession(session),0);
    });
    if(!validPair && !data.session)status("Sign in or register to connect your account.");
  }catch(error){
    status("Account service unavailable: "+safeMessage(error));
  }
}
get("githubLogin").addEventListener("click",()=>continueGitHub(false));
get("linkGitHub").addEventListener("click",()=>continueGitHub(true));
get("approve").addEventListener("click",approve);
get("loginForm").addEventListener("submit",signIn);
get("registerForm").addEventListener("submit",register);
get("saveUsername").addEventListener("click",saveUsername);
get("signOut").addEventListener("click",async()=>{
  if(client){
    // Logout from this browser only: global is Supabase's default and
    // revokes the independent Windows launcher and Mod Packager sessions.
    const {error}=await client.auth.signOut({scope:"local"});
    if(error)status("Sign-out failed: "+safeMessage(error));
    else{await refreshProfile(null);status("Signed out of the Forge Hub website.",true);}
  }
});
get("openStudio").addEventListener("click",()=>{
  location.href="./studio.html";
});
setup();