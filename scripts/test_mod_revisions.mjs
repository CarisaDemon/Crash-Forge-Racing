import test from "node:test";
import assert from "node:assert/strict";
import {readFileSync} from "node:fs";

const get = path => readFileSync(new URL(path, import.meta.url), "utf8");
const studio = get("../docs/creator-studio.js");
const moderation = get("../docs/moderation.js");
const edge = get("../edge_functions/forge-publications/index.ts");
const schema = get("../database/002_mod_revision_publication.sql");
const studioHTML = get("../docs/studio.html");
const modHTML = get("../docs/moderation.html");

test("NEW VERSION points to existing submission instead of a duplicate", () => {
  assert.match(studio, /revisionTarget = \{id:item\.id,title:item\.title/);
  assert.match(studio, /replaces_submission_id:revision \? revision\.id : null/);
  assert.match(studio, /draft\.version === revision\.mod_version/);
  assert.match(studio, /mySubmissions\.some\(item => item\.status === "pending"/);
});

test("public and pending revisions are shown as a single creator card", () => {
  assert.match(studio, /const groups = new Map\(\)/);
  assert.match(studio, /const publicBase = group\.find\(item => item\.is_published\)/);
  assert.match(studio, /const card = addModRow\(base\)/);
  assert.match(studio, /Public v.*?stays available/);
});

test("database revision parent must be approved, same creator and category", () => {
  assert.match(schema, /previous\.status<>'approved'/);
  assert.match(schema, /previous\.owner_id<>NEW\.owner_id/);
  assert.match(schema, /previous\.category<>NEW\.category/);
  assert.match(schema, /previous\.mod_version/);
  assert.match(schema, /forge_one_open_revision_per_prior/);
});

test("public replacement reuses original catalog id and updates in SQL", () => {
  assert.match(schema, /pub_id:=coalesce\(prior_release\.id/);
  assert.match(schema, /update public\.forge_public_mods m/);
  assert.match(schema, /set submission_id=sub\.id/);
  assert.match(schema, /prior_release\.id is not null/);
});

test("reviewed update automatically promotes only after approval", () => {
  assert.match(moderation, /const replacingLive = decision === "approved"/);
  assert.match(moderation, /APPROVE & UPDATE PUBLIC MOD/);
  const review = moderation.indexOf('client.rpc("forge_review_submission"');
  const promotion = moderation.indexOf('const release = await sendReleaseRequest(', review);
  assert.ok(review >= 0 && promotion > review);
  assert.match(moderation, /approved revision|approved and updated|Approved and updated/i);
});

test("verified moderators may publish and unpublish while moderator roster stays owner-only", () => {
  assert.ok(edge.includes('client.rpc("forge_is_moderator")'));
  assert.ok(edge.includes('if(action==="unpublish")'));
  assert.ok(!edge.includes('client.rpc("forge_is_owner")'));
  const block = moderation.slice(moderation.indexOf("function appendPublicationControls"),
    moderation.indexOf("async function sendReleaseRequest"));
  assert.ok(!block.includes("Only the primary owner can publish"));
  assert.ok(!block.includes("Only the primary owner can unpublish"));
  assert.ok(moderation.includes('if (!isOwner || !client) return;'));
});

test("old public ZIP stays alive until atomic catalog replacement", () => {
  const copy = edge.indexOf('const copy=await admin.storage');
  const finalize = edge.indexOf('const finalized=await admin.rpc');
  const cleanup = edge.indexOf('const retired=await admin.storage');
  assert.ok(copy >= 0 && finalize > copy && cleanup > finalize);
  assert.match(edge, /archive_cleanup_pending:archiveCleanupPending/);
});

test("license field removed without claiming rights that were not verified", () => {
  assert.ok(!moderation.includes("PUBLIC RELEASE LICENSE"));
  assert.ok(!moderation.includes("license.value"));
  assert.match(edge, /const PUBLIC_RIGHTS_LABEL="Community upload - rights unverified"/);
  assert.match(edge, /req\?\.publish_confirmed!==true/);
  assert.match(moderation, /I reviewed the sources and any required permission/);
});

test("native track pair archives remain allowed, executable archives blocked", () => {
  assert.match(edge, /"\.lev","\.vrm"/);
  assert.ok(!edge.includes('".exe"'));
});

test("HTML cache-busts both updated scripts", () => {
  assert.match(studioHTML, /creator-studio\.js\?v=20261010_deleted_notices_1/);
  assert.ok(modHTML.includes("moderation.js?v=20261010_permanent_deletion_1"));
});

test("moderators get UNPUBLISH controls and can reverse approval safely", () => {
  const controls = moderation.slice(
    moderation.indexOf("function appendPublicationControls("),
    moderation.indexOf("async function sendReleaseRequest"));
  assert.ok(controls.includes('"UNPUBLISH (KEEP APPROVED)"'));
  assert.ok(!controls.includes("if (!isOwner) {"));
  assert.ok(!controls.includes("Only the primary owner can unpublish"));
  assert.ok(moderation.includes("if (note.length < 8 || note.length > 500)"));
  assert.ok(moderation.includes("cancel.disabled = reject.disabled = !publishedReady;"));
});

test("backend verifies moderator and logs removal reason through trusted SQL", () => {
  const moderatorAccess = edge.indexOf('client.rpc("forge_is_moderator")');
  const unpublishAction = edge.indexOf('if(action==="unpublish")');
  const reasonCheck = edge.indexOf("if(reason.length<8||reason.length>500)");
  const auditCall = edge.indexOf('admin.rpc("forge_remove_publication"');
  assert.ok(moderatorAccess >= 0 && moderatorAccess < unpublishAction);
  assert.ok(unpublishAction < reasonCheck && reasonCheck < auditCall);
  assert.ok(!edge.slice(unpublishAction,edge.indexOf('if(action!=="publish")'))
    .includes('client.rpc("forge_is_owner")'));
  assert.ok(edge.includes('p_submission_id:id,p_reason:reason,p_moderator_id:user.id'));
});
