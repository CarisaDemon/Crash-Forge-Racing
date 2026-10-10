import test from "node:test";
import assert from "node:assert/strict";
import {readFileSync} from "node:fs";

const content = path => readFileSync(new URL(path,import.meta.url),"utf8");
const mod = content("../docs/moderation.js");
const modHTML = content("../docs/moderation.html");
const studio = content("../docs/creator-studio.js");
const studioHTML = content("../docs/studio.html");
const edge = content("../edge_functions/forge-publications/index.ts");
const sql = content("../database/003_secure_mod_deletion_tombstones.sql");

test("moderators can delete any nonpublic mod, including rejected duplicates", () => {
  assert.ok(mod.includes('"DELETE MOD & ZIP"'));
  assert.ok(mod.includes('appendPermanentDeletionControls(card, item);'));
  assert.ok(mod.includes('button.disabled = publishedNow || !publishedReady;'));
  assert.ok(mod.includes('action:"delete_submission"'));
  assert.ok(mod.includes('window.prompt("Type DELETE'));
  assert.ok(mod.includes("reason.length < 8 || reason.length > 500"));
});

test("deleted mods show in the dedicated filter and retry incomplete Storage cleanup", () => {
  assert.ok(modHTML.includes('data-state="deleted"'));
  assert.ok(mod.includes('activeFilter === "deleted" ? !!item.deleted_at'));
  assert.ok(mod.includes('"DELETED / ELIMINADO"'));
  assert.ok(mod.includes('"RETRY ZIP DELETION"'));
  assert.ok(mod.includes('item.zip_deleted_at ? "Private ZIP deleted.'));
  assert.ok(mod.includes('"deleted_at,deletion_reason,deleted_by,zip_deleted_at"'));
});

test("deleted creator card includes reason and enables creator to hide notice", () => {
  assert.ok(studio.includes('if (item.deleted_at)'));
  assert.ok(studio.includes('"ELIMINADO / DELETED"'));
  assert.ok(studio.includes('"Deletion reason: "'));
  assert.ok(studio.includes('"REMOVE FROM MY STUDIO"'));
  assert.ok(studio.includes('button.disabled = !item.zip_deleted_at') ||
    studio.includes('remove.disabled = !item.zip_deleted_at'));
  assert.ok(studio.includes('client.rpc("forge_dismiss_deleted_submission"'));
  assert.match(studio, /if \(item\.deleted_at\) \{\s*target\.append\(addModRow\(item\)\)/);
});

test("deleted assets are retired with ordered Storage deletion, never raw SQL deletion", () => {
  const mark=edge.indexOf('admin.rpc("forge_mark_submission_deleted"');
  const remove=edge.indexOf('admin.storage.from("forge-mod-queue").remove(');
  const complete=edge.indexOf('admin.rpc("forge_complete_submission_deletion"');
  assert.ok(mark>0 && remove>mark && complete>remove);
  assert.ok(!sql.includes("DELETE FROM storage.objects"));
  assert.ok(sql.includes("Private ZIP still exists: removal cannot be confirmed"));
  assert.ok(edge.includes('deletion_recorded:true'));
});

test("auth and audit history are preserved for each permanent deletion", () => {
  assert.ok(edge.includes('client.rpc("forge_is_moderator")'));
  assert.ok(sql.includes("Only a verified moderator can delete submissions"));
  assert.ok(sql.includes("submission_deletion_events"));
  assert.ok(sql.includes("REFERENCES public.mod_submissions(id)"));
  assert.ok(sql.includes("IF EXISTS (SELECT 1 FROM public.forge_public_mods"));
  assert.ok(sql.includes("IF EXISTS (SELECT 1 FROM public.mod_submissions"));
  assert.ok(sql.includes("s.deleted_at IS NULL"));
  assert.ok(sql.includes("deleted_at IS NULL"));
});

test("creator can hide own removed submission only AFTER ZIP purge", () => {
  assert.ok(sql.includes("WHERE id=p_submission_id AND owner_id=uid AND deleted_at IS NOT NULL"));
  assert.ok(sql.includes("AND zip_deleted_at IS NOT NULL"));
  assert.ok(sql.includes("creator_dismissed_at=coalesce(creator_dismissed_at,now())"));
  assert.ok(sql.includes("creator_dismissed_at IS NULL"));
  assert.ok(sql.includes("status='pending' AND deleted_at IS NULL"));
  assert.ok(sql.includes("GRANT EXECUTE ON FUNCTION public.forge_dismiss_deleted_submission"));
});

test("no accidental public downloads or revision resurrection", () => {
  assert.ok(sql.includes("deleted_at IS NULL and not publication_blocked"));
  assert.ok(sql.includes("previous.deleted_at IS NOT NULL"));
  assert.ok(sql.includes("publication_blocked=true"));
  assert.ok(edge.includes('Unpublish this mod before deleting it') ||
    sql.includes("Unpublish this mod before deleting it"));
});

test("both pages cache-bust the fresh JavaScript", () => {
  assert.ok(studioHTML.includes("creator-studio.js?v=20261010_deleted_notices_1"));
  assert.ok(modHTML.includes("moderation.js?v=20261010_permanent_deletion_1"));
});
