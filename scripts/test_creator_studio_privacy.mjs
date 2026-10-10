import test from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import {readFileSync} from "node:fs";

const root=new URL("../",import.meta.url);
const read=relative=>readFileSync(new URL(relative,root),"utf8");
const source=read("docs/forge-hub/creator-studio.js");
const html=read("docs/forge-hub/studio.html");
const migration=read("database/004_private_creator_submissions.sql");

function functionSource(name,next) {
 const start=source.indexOf("async function "+name+"(");
 const stop=source.indexOf(next,start);
 assert.ok(start>=0 && stop>start,"Missing isolated "+name);
 return source.slice(start,stop);
}

const fetchFunction=functionSource("loadMyMods","async function saveProfile(");
const dismissFunction=functionSource("dismissDeletedSubmission","function showSubmissionEditor(");

test("database owner-only view blocks moderators' cross-account records",()=>{
 assert.match(migration,/WITH \(security_invoker = true, security_barrier = true\)/);
 assert.match(migration,/WHERE owner_id = \(SELECT auth\.uid\(\)\)/);
 assert.match(migration,/creator_dismissed_at IS NULL/);
 assert.match(migration,/REVOKE ALL ON public\.forge_my_submissions FROM PUBLIC, anon/);
 assert.match(migration,/GRANT SELECT ON public\.forge_my_submissions TO authenticated/);
 assert.doesNotMatch(migration,/DROP POLICY forge_moderator_submissions_read/);
});

test("web uses the private database view and filters both owner and dismissal",()=>{
 assert.match(fetchFunction,/client\.from\("forge_my_submissions"\)/);
 assert.match(fetchFunction,/\.eq\("owner_id", ownerId\)/);
 assert.match(fetchFunction,/\.is\("creator_dismissed_at", null\)/);
 assert.match(fetchFunction,/item\.owner_id === ownerId && item\.creator_dismissed_at === null/);
 assert.doesNotMatch(fetchFunction,/client\.from\("mod_submissions"\)/);
 assert.match(html,/creator-studio\.js\?v=20261010_id_at_upload/);
});

function fixture(rows,user="creator-a",deferred=null) {
 const calls=[];
 const results=[];
 const nodes={
   myModsLabel:{textContent:""},
   myModsList:{replaceChildren(){results.length=0;},
               append(x){results.push(x);},
               prepend(x){results.unshift(x);},
               querySelector(){return null;}}
 };
 const query = {
   select(...args){calls.push(["select",...args]);return this;},
   eq(...args){calls.push(["eq",...args]);return this;},
   is(...args){calls.push(["is",...args]);return this;},
   order(...args){calls.push(["order",...args]);return this;},
   limit(...args){calls.push(["limit",...args]);return deferred || Promise.resolve({data:rows,error:null});}
 };
 const publicQuery={
   select(...args){calls.push(["public.select",...args]);return this;},
   in(...args){calls.push(["public.in",...args]);return Promise.resolve({data:[],error:null});}
 };
 const ctx={
   client:{from(name){calls.push(["from",name]);
     return name==="forge_my_submissions"?query:publicQuery;}},
   activeUser:{id:user}, activeProfile:{},
   myModsLoadSerial:0,mySubmissions:[],
   el:name=>nodes[name],
   renderLocalDraft(){},
   addModRow:item=>({id:item.id,append(x){this.child=x;}}),
   make:(tag,cls,text)=>({tag,cls,text}),
   Set, Map, Date
 };
 vm.createContext(ctx);
 vm.runInContext(fetchFunction,ctx);
 return {ctx,results,calls,nodes};
}

test("a moderator's My Mods still shows only THEIR own records",async()=>{
 const rows=[
  {id:"mine",owner_id:"creator-a",creator_dismissed_at:null,status:"approved"},
  {id:"someone-else",owner_id:"creator-b",creator_dismissed_at:null,status:"approved"},
  {id:"already-hidden",owner_id:"creator-a",creator_dismissed_at:"2026-10-10",status:"rejected"}
 ];
 const f=fixture(rows);
 await vm.runInContext("loadMyMods()",f.ctx);
 assert.deepEqual(f.results.map(x=>x.id),["mine"]);
 assert.deepEqual(f.ctx.mySubmissions.map(x=>x.id),["mine"]);
 assert.ok(f.calls.some(x=>x[0]==="from"&&x[1]==="forge_my_submissions"));
 assert.ok(f.calls.some(x=>x[0]==="eq"&&x[1]==="owner_id"&&x[2]==="creator-a"));
 assert.ok(f.calls.some(x=>x[0]==="is"&&x[1]==="creator_dismissed_at"&&x[2]===null));
 assert.equal(f.nodes.myModsLabel.textContent,"1 SUBMISSIONS");
});

test("old session's delayed query can never paint the new account's list",async()=>{
 let release;
 const remote = new Promise(resolve=>{release=resolve;});
 const f=fixture([{id:"private-old",owner_id:"creator-a",creator_dismissed_at:null}], "creator-a", remote);
 const loading=vm.runInContext("loadMyMods()",f.ctx);
 f.ctx.activeUser={id:"creator-b"};
 f.ctx.myModsLoadSerial++;
 release({data:[{id:"private-old",owner_id:"creator-a",creator_dismissed_at:null}],error:null});
 await loading;
 assert.deepEqual(f.results,[]);
 assert.deepEqual(f.ctx.mySubmissions,[]);
});

function dismissFixture({owner="owner-1",rpcResult={data:true,error:null},withUser=true}={}) {
 const item={id:"old-mod",title:"Removed Mod",owner_id:owner,deleted_at:"2026-10-10",
             zip_deleted_at:"2026-10-10"};
 let rpcCount=0,removed=false,refreshed=false,status="";
 const button={
   disabled:false,
   closest(selector){assert.equal(selector,".mod-row");
     return {remove(){removed=true;}};}
 };
 const feedback={textContent:""};
 const scope={
    client:{async rpc(action,payload){
      rpcCount++;
      assert.equal(action,"forge_dismiss_deleted_submission");
      assert.equal(payload.p_submission_id,"old-mod");
      return rpcResult;
    }},
    activeUser:withUser?{id:"owner-1"}:null,
    mySubmissions:[{id:"old-mod"},{id:"other"}],
    window:{confirm(){return true;}},
    message(text){status=text;},
    async loadMyMods(){refreshed=true;}
 };
 vm.createContext(scope);
 vm.runInContext(dismissFunction,scope);
 return {
   item,button,feedback,scope,
   run:()=>vm.runInContext("dismissDeletedSubmission",scope)(item,button,feedback),
   outcome:()=>({rpcCount,removed,refreshed,status,visibleIds:scope.mySubmissions.map(x=>x.id)})
 };
}

test("creator dismissal removes their card after a confirmed RPC",async()=>{
 const f=dismissFixture();
 await f.run();
 assert.equal(f.outcome().rpcCount,1);
 assert.equal(f.outcome().removed,true);
 assert.equal(f.outcome().refreshed,true);
 assert.deepEqual(f.outcome().visibleIds,["other"]);
 assert.match(f.outcome().status,/removed from your Creator Studio/i);
});

test("creator dismissal error is visible beside the button and can retry",async()=>{
 const f=dismissFixture({rpcResult:{data:null,error:{message:"Network unavailable"}}});
 await f.run();
 assert.equal(f.outcome().rpcCount,1);
 assert.equal(f.outcome().removed,false);
 assert.equal(f.button.disabled,false);
 assert.match(f.feedback.textContent,/Network unavailable/);
 assert.equal(f.outcome().refreshed,false);
});

test("a moderator cannot dismiss a mod that belongs to another creator",async()=>{
 const f=dismissFixture({owner:"creator-b"});
 await f.run();
 assert.equal(f.outcome().rpcCount,0);
 assert.equal(f.outcome().removed,false);
 assert.match(f.feedback.textContent,/Only the creator/i);
});

test("deletion notices stay in the studio until the creator dismisses them",()=>{
 assert.match(source,/item\.deleted_at/);
 assert.match(source,/"ELIMINADO \/ DELETED"/);
 assert.match(source,/"Deletion reason: "/);
 assert.match(source,/"REMOVE FROM MY STUDIO"/);
 assert.match(source,/remove\.disabled = !item\.zip_deleted_at/);
 assert.match(source,/feedback\.setAttribute\("aria-live", "polite"\)/);
});
