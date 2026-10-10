/* Forge Hub moderator UI.
   This page offers no client-side admin privilege: trusted GitHub OAuth
   identity is verified by Supabase SQL; RLS protects all other creators.
   Approval updates review status only, and NEVER publishes a ZIP publicly. */
"use strict";

const get = id => document.getElementById(id);
const node = (tag, className, text) => {
    const element = document.createElement(tag);
    if (className) element.className = className;
    if (text !== undefined) element.textContent = String(text);
    return element;
};
const MAX_VISIBLE = 200;
const formatBytes = bytes => Number(bytes) < 1048576 ?
    (Number(bytes) / 1024).toFixed(1) + " KB" :
    (Number(bytes) / 1048576).toFixed(2) + " MB";

let client = null;
let currentUser = null;
let entries = [];
let creators = new Map();
let activeFilter = "all";
let busy = false;
let published = new Map();
let publishedReady = false;
let publicApiKey = "";
let isOwner = false;
const AUDIT_PAGE_SIZE = 50;
let auditEntries = [];
let auditCursor = null;
let auditHasMore = true;
let auditBusy = false;
const PUBLICATION_URL = "https://mjvpkerobjgoldmimyxz.supabase.co/functions/v1/forge-publications";

function banner(text, state = "info") {
    const el = get("moderationBanner");
    el.textContent = text;
    el.className = "notice " + state;
}
function showSignIn(message, hasSession = false) {
    get("reviewPanel").classList.add("hidden");
    get("refreshReviews").disabled = true;
    const card = get("signInHelp");
    card.classList.remove("hidden");
    const title = card.querySelector("h2");
    const help = card.querySelector("p");
    title.textContent = hasSession ? "Moderator access required" : "Sign in with GitHub";
    help.textContent = message;
    banner(message, "info");
}
const filenameForDownload = value => {
    const cleaned = String(value || "submission.zip")
        .replace(/[^A-Za-z0-9_.\- ]/g, "_").slice(0, 120);
    return /\.zip$/i.test(cleaned) ? cleaned : "forge-hub-submission.zip";
};
const safeGithubLogin = value =>
    /^[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?$/.test(String(value || ""));
function makeMeta(label, value) {
    const group = node("div", "moderation-meta");
    group.append(node("span", "meta", label), node("div", "moderation-value", value));
    return group;
}
async function loadModeratorRoster() {
    if (!isOwner || !client) return;
    const target = get("moderatorRoster");
    target.replaceChildren(node("p", "muted", "Loading verified GitHub moderators..."));
    const result = await client.rpc("forge_list_moderators");
    if (result.error) {
        target.replaceChildren(node("p", "moderation-message",
            "Could not load moderators: " + result.error.message));
        return;
    }
    target.replaceChildren();
    for (const entry of result.data || []) {
        const member = node("div", "moderator-member");
        const details = node("div");
        const login = entry.github_login || "Unknown GitHub account";
        const name = node("strong", "", "@" + login);
        const role = node("span", "moderator-role", entry.member_role === "owner" ? "OWNER" : "MODERATOR");
        details.append(name, role);
        details.append(node("p", "caption", (entry.display_name || "") +
            " · GitHub ID " + String(entry.github_id)));
        member.append(details);
        if (entry.member_role === "owner") {
            member.append(node("span", "caption", "PROTECTED"));
        } else {
            const revoke = node("button", "btn ghost", "REMOVE");
            revoke.type = "button";
            revoke.addEventListener("click", () => manageModerator(login, "revoke", revoke));
            member.append(revoke);
        }
        target.append(member);
    }
    if (!result.data || !result.data.length) {
        target.append(node("p", "muted", "No verified moderators found."));
    }
}
async function manageModerator(login, action, button) {
    if (!isOwner || busy || !client || !safeGithubLogin(login)) return;
    if (action === "revoke" && !window.confirm(
        "Revoke moderator permissions from @" + login + "? This takes effect immediately.")) return;
    busy = true;
    button.disabled = true;
    const feedback = get("moderatorAdminMessage");
    feedback.textContent = action === "grant" ? "Adding verified moderator..." : "Revoking moderator access...";
    try {
        const result = await client.rpc("forge_manage_moderator", {
            p_github_login:login, p_action:action
        });
        if (result.error) throw result.error;
        if (!Array.isArray(result.data) || result.data.length !== 1)
            throw new Error("Server did not confirm the moderator change.");
        feedback.textContent = "@" + login + (action === "grant" ?
            " can now review submissions." : " no longer has moderator access.");
        if (action === "grant") get("moderatorGithub").value = "";
        await loadModeratorRoster();
        await loadModeratorAudit(true);
    } catch (error) {
        feedback.textContent = "Moderator update failed: " + String(error.message || error);
    } finally {
        busy = false;
        button.disabled = false;
    }
}
const AUDIT_GROUPS = {
    reviews:["approved","rejected","approval_cancelled","approval_revoked","status_changed"],
    holds:["publication_locked","publication_unlocked"],
    releases:["published","republished","unpublished"],
    accounts:["moderator_added","moderator_removed"],
    downloads:["zip_download_requested"]
};
function renderModeratorAudit() {
    if (!isOwner) return;
    const filter = get("moderatorAuditFilter").value;
    const query = get("moderatorAuditSearch").value.trim().toLowerCase();
    const allowed = AUDIT_GROUPS[filter] || null;
    const visible = auditEntries.filter(item =>
        (!allowed || allowed.includes(item.event_type)) &&
        (!query || [
            item.actor_login,item.actor_github_id,item.actor_role,
            item.event_type,item.target_title,item.target_id,
            item.before_state,item.after_state,item.details
        ].some(x => String(x == null ? "" : x).toLowerCase().includes(query)))
    );
    const target = get("moderatorAuditEntries");
    target.replaceChildren();
    if (!visible.length) {
        target.append(node("p", "muted", auditEntries.length ?
            "No loaded audit events match these filters." :
            "No audit events recorded yet. Actions performed before audit activation are not backfilled."));
    } else {
        const table = node("table", "moderator-audit-table");
        const head = node("thead");
        const headRow = node("tr");
        for (const header of ["DATE / TIME","MODERATOR","ACTION","TARGET","STATE CHANGE","NOTES / DETAILS"]) {
            const th = node("th", "", header);
            th.scope = "col";
            headRow.append(th);
        }
        head.append(headRow);
        const body = node("tbody");
        for (const item of visible) {
            const tr = node("tr");
            const date = item.happened_at ?
                new Date(item.happened_at).toLocaleString() : "Unknown date";
            const actor = item.actor_login ?
                "@" + item.actor_login : (item.actor_github_id ?
                "GitHub #" + item.actor_github_id : "System / unknown");
            const change = item.before_state || item.after_state ?
                String(item.before_state || "—") + " → " +
                String(item.after_state || "—") : "—";
            const columns = [
                [date,""],
                [actor + " (" + String(item.actor_role || "unknown") + ")","audit-actor"],
                [String(item.event_type || "").replace(/_/g," ").toUpperCase(),"audit-action"],
                [String(item.target_title || item.target_id || "—"),""],
                [change,""],
                [String(item.details || "—"),"audit-detail"]
            ];
            for (const [value, className] of columns) tr.append(node("td",className,value));
            body.append(tr);
        }
        table.append(head,body);
        target.append(table);
    }
    get("moderatorAuditStatus").textContent = visible.length +
        " matching entries / " + auditEntries.length + " loaded. " +
        (auditHasMore ? "Older records available." : "End of available history.");
    get("moderatorAuditMore").disabled = auditBusy || !auditHasMore;
}
async function loadModeratorAudit(reset = false) {
    if (!isOwner || !client || auditBusy) return;
    auditBusy = true;
    const refresh = get("moderatorAuditRefresh");
    const more = get("moderatorAuditMore");
    refresh.disabled = more.disabled = true;
    if (reset) {
        auditEntries = [];
        auditCursor = null;
        auditHasMore = true;
    }
    get("moderatorAuditStatus").textContent = "Loading private moderator activity...";
    try {
        const response = await client.rpc("forge_list_moderation_activity", {
            p_limit:AUDIT_PAGE_SIZE, p_before_id:auditCursor
        });
        if (response.error) throw response.error;
        const batch = Array.isArray(response.data) ? response.data : [];
        auditEntries.push(...batch);
        if (batch.length) auditCursor = batch[batch.length - 1].event_id;
        auditHasMore = batch.length === AUDIT_PAGE_SIZE;
        auditBusy = false;
        renderModeratorAudit();
    } catch (error) {
        auditBusy = false;
        get("moderatorAuditStatus").textContent =
            "Audit history unavailable: " + String(error.message || error);
        more.disabled = true;
    } finally {
        refresh.disabled = false;
    }
}
function countStatuses() {
    for (const [id, status] of [
        ["countAll", "all"],
        ["countPending", "pending"],
        ["countApproved", "approved"],
        ["countRejected", "rejected"],
        ["countPublished", "published"]
    ]) {
        get(id).textContent = status === "all" ? entries.length :
            status === "published" ? published.size :
            entries.filter(item => item.status === status).length;
    }
    get("queueTotal").textContent = entries.length + " REQUESTS / PRIVATE";
}
function ownerHandle(item) {
    const author = creators.get(item.owner_id);
    return author && safeGithubLogin(author.github_login) ?
        "@" + author.github_login : "Creator (profile unavailable)";
}
function render() {
    countStatuses();
    const search = get("reviewSearch").value.trim().toLowerCase();
    const filtered = entries.filter(item =>
        (activeFilter === "all" || (activeFilter === "published" ? published.has(item.id) : item.status === activeFilter)) &&
        (!search || [
            item.title, item.category, item.description,
            item.original_filename, ownerHandle(item)
        ].some(value => String(value || "").toLowerCase().includes(search)))
    );
    const target = get("reviewList");
    target.replaceChildren();
    if (!filtered.length) {
        target.append(node("div", "empty", activeFilter === "pending" ?
            "There are no pending review requests." :
            "No submissions match this filter."));
        return;
    }
    for (const item of filtered) target.append(makeCard(item));
}
function makeCard(item) {
    const card = node("article", "moderation-item");
    const header = node("div", "moderation-header");
    const left = node("div");
    left.append(node("h3", "", item.title));
    const subtitle = node("p", "muted", item.category + " • " +
        (item.mod_version || "1.0") + " • " + ownerHandle(item));
    left.append(subtitle);
    const status = node("span", "status-chip " +
        (item.status === "approved" ? "verified" :
         item.status === "rejected" ? "rejected" : "pending"),
        (item.status || "unknown").toUpperCase());
    header.append(left, status);
    card.append(header);
    const owner = creators.get(item.owner_id);
    if (owner && safeGithubLogin(owner.github_login)) {
        const link = node("a", "moderation-creator-link", "VIEW CREATOR ON GITHUB ↗");
        link.href = "https://github.com/" + encodeURIComponent(owner.github_login);
        link.target = "_blank";
        link.rel = "noopener noreferrer";
        card.append(link);
    }
    card.append(node("p", "moderation-description", item.description || "No description provided."));
    card.append(node("div", "mod-type-detail",
        window.ForgeModMeta ?
        window.ForgeModMeta.summary(item.category, {...item, type:item.category}) :
        "Mod category: " + item.category));

    const details = node("div", "moderation-details");
    details.append(
        makeMeta("ORIGINAL ZIP", item.original_filename || "Unknown filename"),
        makeMeta("ZIP SIZE", formatBytes(item.file_size_bytes || 0)),
        makeMeta("SUBMITTED", item.created_at ? new Date(item.created_at).toLocaleString() : "Unknown"),
        makeMeta("SHA-256", item.sha256 || "Missing fingerprint")
    );
    card.append(details);

    const message = node("p", "moderation-message");
    message.setAttribute("role", "status");
    const download = node("button", "btn secondary", "DOWNLOAD / VERIFY ZIP");
    download.type = "button";
    download.addEventListener("click", () => downloadSubmission(item, download, message));
    card.append(download, message);

    if (item.status === "pending") {
        const review = node("div", "moderation-review");
        const label = node("label", "moderation-field");
        label.append(node("span", "meta", "REVIEW NOTE"));
        const note = node("textarea");
        note.maxLength = 500;
        note.placeholder = "For example: Please clarify which game or creator the models and textures came from. A reason is required for rejection.";
        label.append(note);
        review.append(label);

        const rightsLabel = node("label", "checkbox-row");
        const rights = node("input");
        rights.type = "checkbox";
        rightsLabel.append(rights,
            document.createTextNode(
                " I have checked the package and verified the creator has redistribution rights (required to approve)."));
        review.append(rightsLabel);
        const actions = node("div", "form-actions");
        const approve = node("button", "btn", "APPROVE");
        const reject = node("button", "btn ghost", "REJECT");
        approve.type = reject.type = "button";
        approve.addEventListener("click", () =>
            reviewSubmission(item, "approved", note.value, rights.checked, [approve, reject], message));
        reject.addEventListener("click", () =>
            reviewSubmission(item, "rejected", note.value, false, [approve, reject], message));
        actions.append(approve, reject);
        review.append(actions);
        card.append(review);
    } else {
        const result = node("p", "caption",
            item.reviewed_at ? "Reviewed " + new Date(item.reviewed_at).toLocaleString() :
            "This request was reviewed outside the Forge Hub panel.");
        card.append(result);
        if (item.moderator_note) card.append(node("p", "inset", "Moderator note: " + item.moderator_note));
    }
    if (item.status === "approved") appendApprovalReversalControls(card, item);
    appendPublicationControls(card, item);
    return card;
}

function appendApprovalReversalControls(card, item) {
    // An approved request stays reversible. Any already-public ZIP must be
    // removed by the trusted backend before its review status can change.
    const panel = node("div", "moderation-review");
    panel.append(node("h4", "", "REVIEW AN APPROVED MOD"));
    panel.append(node("p", "caption", published.has(item.id) ?
        "This mod is publicly released. Cancelling or rejecting automatically unpublishes its downloadable ZIP first." :
        "You can cancel approval and send this mod back to Pending, or reject it with a message."));
    const field = node("label", "moderation-field");
    field.append(node("span", "meta", "MODERATOR NOTE"));
    const note = node("textarea");
    note.maxLength = 500;
    note.placeholder = "Optional if cancelling approval; required if rejecting (at least 8 characters).";
    field.append(note);
    panel.append(field);
    const controls = node("div", "form-actions");
    const cancel = node("button", "btn secondary", "CANCEL APPROVAL");
    const reject = node("button", "btn ghost", "REJECT MOD");
    const info = node("p", "moderation-message");
    info.setAttribute("role", "status");
    cancel.type = reject.type = "button";
    cancel.disabled = reject.disabled = !publishedReady ||
        (published.has(item.id) && !isOwner);
    cancel.addEventListener("click", () =>
        reviewSubmission(item, "pending", note.value, false, [cancel, reject], info));
    reject.addEventListener("click", () =>
        reviewSubmission(item, "rejected", note.value, false, [cancel, reject], info));
    controls.append(cancel, reject);
    panel.append(controls, info);
    card.append(panel);
}
async function changePublicationHold(item, hold, button, feedback) {
    if (!client || !currentUser || busy) return;
    if (published.has(item.id)) {
        feedback.textContent = "Unpublish first.";
        return;
    }
    if (!window.confirm(hold ? "Relock publication?" : "Unlock publication? This does not publish the ZIP.")) return;
    busy = true;
    button.disabled = true;
    try {
        const result = await client.rpc("forge_set_publication_hold", {
            p_submission_id: item.id, p_hold: hold,
            p_note: hold ? "Moderator relocked this submission" : "Moderator reviewed source attribution"
        });
        if (result.error) throw result.error;
        if (!result.data || result.data.length !== 1 ||
            result.data[0].publication_blocked !== hold) throw new Error("Server did not confirm the change.");
        await loadQueue();
    } catch (error) {
        feedback.textContent = "Publication lock error: " + String(error.message || error);
    } finally {
        button.disabled = false;
        busy = false;
    }
}
function appendPublicationControls(card, item) {
    const released = published.get(item.id);
    if (released) {
        const section = node("div", "moderation-published");
        section.append(node("span", "status-chip verified", "PUBLIC RELEASE"));
        const link = node("a", "small-link", "OPEN PUBLIC ZIP ↗");
        link.href = released.download_url;
        link.target = "_blank";
        link.rel = "noopener noreferrer";
        section.append(link);
        section.append(node("p", "caption", "Published to Forge Hub; also visible on the creator profile and in the online launcher catalog."));
        if (!isOwner) {
            section.append(node("p", "caption", "Only the primary owner can unpublish an active public release."));
            card.append(section);
            return;
        }
        const unpublishReason = node("textarea");
        unpublishReason.maxLength = 500;
        unpublishReason.placeholder = "Why is this public release being removed? (8-500 characters)";
        const unpublishLabel = node("label", "moderation-field");
        unpublishLabel.append(node("span", "meta", "TAKEDOWN REASON"), unpublishReason);
        const unpublishButton = node("button", "btn ghost", "UNPUBLISH (KEEP APPROVED)");
        unpublishButton.type = "button";
        unpublishButton.disabled = !publishedReady;
        const feedback = node("p", "moderation-message");
        feedback.setAttribute("role", "status");
        unpublishButton.addEventListener("click", () =>
            unpublishSubmission(item, unpublishReason.value, unpublishButton, feedback));
        section.append(unpublishLabel, unpublishButton, feedback);
        card.append(section);
        return;
    }
    if (item.status !== "approved") return;
    if (item.publication_blocked) {
        const warning = node("div", "moderation-publication-blocked");
        warning.append(node("strong", "", "PUBLICATION BLOCKED"));
        warning.append(node("p", "",
            "This mod is approved for private review but public release is locked. Check the original source credit in the description before deciding whether to unlock. Credit does not itself grant a redistribution license."));
        const unlock = node("button", "btn secondary", "UNLOCK PUBLICATION");
        unlock.type = "button";
        const feedback = node("p", "moderation-message");
        feedback.setAttribute("role", "status");
        unlock.addEventListener("click", () =>
            changePublicationHold(item, false, unlock, feedback));
        warning.append(unlock, feedback);
        card.append(warning);
        return;
    }
    if (!publishedReady) {
        card.append(node("p", "caption", "Publication status unavailable. Retry when the public catalog reconnects."));
        return;
    }
    if (!isOwner) {
        card.append(node("p", "caption", "Reviewed and unlocked. Only the primary owner can publish this ZIP publicly."));
        return;
    }
    const section = node("div", "moderation-publish");
    section.append(node("h4", "", "PUBLIC RELEASE — SEPARATE FROM APPROVAL"));
    const holdButton = node("button", "btn ghost", "RELOCK PUBLICATION");
    holdButton.type = "button";
    const holdFeedback = node("p", "moderation-message");
    holdFeedback.setAttribute("role", "status");
    holdButton.addEventListener("click", () =>
        changePublicationHold(item, true, holdButton, holdFeedback));
    section.append(holdButton, holdFeedback);
    section.append(node("p", "caption",
        "Publishing makes the ZIP downloadable by everyone. Publish only original creations or content with explicit written redistribution permission."));
    const sourceNotes = String(item.description || "").trim();
    const gameRip = /\b(ripped|extracted|ported|taken)\s+from\s+\S+/i.test(sourceNotes);
    const releaseLabel = gameRip ? "Game rip - source credited" : "All rights reserved";
    const source = node("div", "moderation-source-credit");
    source.append(
        node("strong", "", gameRip ? "GAME ASSET / ORIGINAL SOURCE CREDITED" : "CREATOR RELEASE"),
        node("p", "", sourceNotes)
    );
    section.append(source);
    section.append(node("p", "caption",
        gameRip ?
        "This is a community game rip, not an original model. Credits identify the source but do not grant distribution rights; a rights holder may request its removal." :
        "You are responsible for reviewing the description and verifying that the proposed public release is appropriate."));
    const basisLabel = node("label", "moderation-field");
    basisLabel.append(node("span", "meta", gameRip ?
        "ORIGINAL SOURCE / REVIEW NOTES (OPTIONAL, MAX 1000 CHARACTERS)" :
        "RIGHTS EVIDENCE (OPTIONAL, MAX 1000 CHARACTERS)"));
    const basis = node("textarea");
    basis.maxLength = 1000;
    basis.placeholder = gameRip ?
        "Identify the original game and any relevant context for this community mod." :
        "Explain your right or permission to redistribute the mod publicly.";
    if (gameRip) basis.value = ("Original source credited by creator: " + sourceNotes).slice(0,1000);
    basisLabel.append(basis);
    section.append(basisLabel);
    const licenseLabel = node("label", "moderation-field");
    licenseLabel.append(node("span", "meta", "PUBLIC RELEASE LICENSE"));
    const license = node("select", "moderation-license");
    for (const [value, title] of (gameRip ? [
        ["Game rip - source credited", "Game rip — original source credited"]
    ] : [
        ["All rights reserved", "All rights reserved — permission verified"],
        ["CC-BY-4.0", "Creative Commons Attribution 4.0"],
        ["CC0-1.0", "Creative Commons Zero 1.0"]
    ])) {
        const option = node("option", "", title);
        option.value = value;
        license.append(option);
    }
    licenseLabel.append(license);
    section.append(licenseLabel);
    const confirmation = node("label", "checkbox-row");
    const checkbox = node("input");
    checkbox.type = "checkbox";
    confirmation.append(checkbox, document.createTextNode(gameRip ?
        " I reviewed the source attribution and understand credit is not a license. I accept responsibility for this public release and will honor valid takedown requests." :
        " I verified the creator has rights to distribute these files publicly."));
    section.append(confirmation);
    const resultMessage = node("p", "moderation-message");
    resultMessage.setAttribute("role", "status");
    const button = node("button", "btn moderation-publish-button", "PUBLISH TO FORGE HUB");
    button.type = "button";
    button.addEventListener("click", () =>
        publishSubmission(item, basis.value, license.value, checkbox.checked, button, resultMessage));
    section.append(button, resultMessage);
    card.append(section);
}
async function sendReleaseRequest(payload) {
    const auth = await client.auth.getSession();
    const token = auth.data?.session?.access_token;
    if (auth.error || !token) throw new Error("Sign in with GitHub again.");
    const response = await fetch(PUBLICATION_URL, {
        method:"POST", mode:"cors",
        headers:{
            "Content-Type":"application/json",
            "Authorization":"Bearer " + token,
            "apikey":publicApiKey
        },
        body:JSON.stringify(payload)
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) {
        throw new Error(String(data.error ||
            "Publication service returned HTTP " + response.status));
    }
    return data;
}
async function unpublishSubmission(item, reason, button, feedback) {
    if (!client || !currentUser || busy || !publishedReady || !published.has(item.id)) return;
    const note = String(reason || "").trim();
    if (note.length < 8 || note.length > 500) {
        feedback.textContent = "Provide a takedown reason of 8 to 500 characters.";
        return;
    }
    if (!window.confirm("Remove the public ZIP and listing for " + item.title +
        "? The private review status will remain Approved.")) return;
    busy = true;
    button.disabled = true;
    feedback.textContent = "Removing the public download and catalog listing...";
    try {
        const reply = await sendReleaseRequest({
            action:"unpublish", submission_id:item.id, reason:note
        });
        if (reply.removed !== true) throw new Error("The server did not confirm removal.");
        published.delete(item.id);
        banner("Removed " + item.title +
            " from the public catalog. Its private approval is unchanged.", "ok");
        await loadQueue();
    } catch (error) {
        feedback.textContent = "Unpublish failed: " + String(error.message || error);
    } finally {
        busy = false;
        button.disabled = false;
    }
}
async function publishSubmission(item, evidence, license, confirmed, button, feedback) {
    if (!client || !currentUser || busy || !publishedReady || published.has(item.id)) return;
    const basis = String(evidence || "").trim();
    if (item.status !== "approved" || item.publication_blocked) {
        feedback.textContent = "Only approved and unlocked requests can be published.";
        return;
    }
    if (!confirmed || basis.length > 1000) {
        feedback.textContent = "Confirm redistribution rights. Optional notes must be at most 1000 characters.";
        return;
    }
    if (!window.confirm(license === "Game rip - source credited" ?
        "This ZIP includes original game assets. Attribution alone is not permission to redistribute them. Publish the ZIP publicly anyway under your responsibility?" :
        "This makes the ZIP publicly downloadable. Have you verified your distribution rights?")) return;
    busy = true;
    button.disabled = true;
    const oldLabel = button.textContent;
    button.textContent = "VERIFYING AND PUBLISHING...";
    feedback.textContent = "Checking original ZIP SHA-256 and releasing through the secure service...";
    try {
        const auth = await client.auth.getSession();
        const token = auth.data?.session?.access_token;
        if (auth.error || !token) throw new Error("Sign in with GitHub again.");
        const response = await fetch(PUBLICATION_URL, {
            method: "POST",
            mode: "cors",
            headers: {
                "Content-Type": "application/json",
                "Authorization": "Bearer " + token,
                "apikey": publicApiKey
            },
            body: JSON.stringify({
                action: "publish", submission_id: item.id,
                moderator_notes: basis, license, publish_confirmed: true
            })
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok || data.published !== true) {
            throw new Error(String(data.error || "Publication service rejected this request (HTTP " + response.status + ")."));
        }
        banner("Published " + item.title + " to the public Forge Hub catalog.", "ok");
        await loadQueue();
    } catch (error) {
        feedback.textContent = "Publication failed: " + String(error.message || error);
        button.disabled = false;
        button.textContent = oldLabel;
    } finally {
        busy = false;
    }
}

async function downloadSubmission(item, button, message) {
    if (!client || !currentUser || busy) return;
    busy = true;
    button.disabled = true;
    const originalText = button.textContent;
    button.textContent = "VERIFYING PRIVATE ZIP...";
    message.textContent = "Reading the private package and verifying SHA-256...";
    try {
        const audit = await client.rpc("forge_log_moderator_download", {
            p_submission_id:item.id
        });
        if (audit.error || audit.data !== true) {
            throw new Error("Download audit could not be recorded. Please retry.");
        }
        // Supabase RLS only lets the verified moderator read others' ZIPs.
        const result = await client.storage.from("forge-mod-queue").download(item.zip_path);
        if (result.error) throw result.error;
        const blob = result.data;
        if (!blob || blob.size !== Number(item.file_size_bytes)) {
            throw new Error("Package size differs from the submission record.");
        }
        if (blob.size > 50 * 1024 * 1024) {
            throw new Error("The ZIP exceeds the review size limit.");
        }
        const digest = await crypto.subtle.digest("SHA-256", await blob.arrayBuffer());
        const sha = Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, "0")).join("");
        if (!/^[a-f0-9]{64}$/i.test(String(item.sha256 || "")) ||
            sha !== String(item.sha256).toLowerCase()) {
            throw new Error("SHA-256 mismatch: ZIP download blocked.");
        }
        const url = URL.createObjectURL(blob);
        const anchor = node("a");
        anchor.href = url;
        anchor.download = filenameForDownload(item.original_filename);
        anchor.hidden = true;
        document.body.append(anchor);
        anchor.click();
        anchor.remove();
        window.setTimeout(() => URL.revokeObjectURL(url), 15000);
        message.textContent = "ZIP checksum verified. Download started; review the archive safely.";
    } catch (error) {
        message.textContent = "Download failed: " + (error.message || String(error));
    } finally {
        button.disabled = false;
        button.textContent = originalText;
        busy = false;
    }
}
async function reviewSubmission(item, decision, note, rightsConfirmed, buttons, message) {
    if (!client || !currentUser || busy) return;
    const trimmed = String(note || "").trim();
    if (decision === "approved" && !rightsConfirmed) {
        message.textContent = "Confirm redistribution rights and package review before approving.";
        return;
    }
    if (decision === "rejected" && trimmed.length < 8) {
        message.textContent = "Provide a rejection reason (at least 8 characters).";
        return;
    }
    const publicRelease = published.has(item.id);
    const confirmText = decision === "approved" ?
        "Approve this PRIVATE request? You must have verified the creator's content rights. This does not publish its ZIP." :
        decision === "pending" ?
        "Cancel this approval and return the mod to Pending review? The creator will be able to revise the submission." :
        "Reject this mod? The creator will see your review note.";
    const extraWarning = publicRelease ?
        " IMPORTANT: This mod is PUBLIC. The public ZIP and listing will be removed before changing its approval." : "";
    if (!window.confirm(confirmText + extraWarning)) return;
    busy = true;
    for (const button of buttons) button.disabled = true;
    message.textContent = "Saving " + decision + " decision...";
    try {
        if (publicRelease) {
            const reason = decision === "rejected" ? trimmed :
                (trimmed.length >= 8 ? trimmed : "Approval cancelled by moderator");
            const response = await sendReleaseRequest({
                action:"unpublish", submission_id:item.id, reason
            });
            if (response.removed !== true) throw new Error("Could not unpublish this mod.");
            published.delete(item.id);
        }
        const result = await client.rpc("forge_review_submission", {
            p_submission_id:item.id,
            p_decision:decision,
            p_note:trimmed
        });
        if (result.error) throw result.error;
        if (!Array.isArray(result.data) || result.data.length !== 1 ||
            result.data[0].review_status !== decision) {
            throw new Error("Server did not confirm the requested state.");
        }
        banner("Review saved: " + item.title + " → " + decision.toUpperCase() +
            ". This does not publish any files.", "ok");
        await loadQueue();
    } catch (error) {
        message.textContent = "Review failed: " + (error.message || String(error));
        for (const button of buttons) button.disabled = false;
    } finally {
        busy = false;
    }
}

async function loadQueue() {
    if (!client || !currentUser) return;
    get("refreshReviews").disabled = true;
    const result = await client.from("mod_submissions").select(
        "id,owner_id,title,category,mod_version,description,map_kind,racer_class," +
        "kart_drive,wheel_setup,original_filename,zip_path,sha256,file_size_bytes," +
        "status,moderator_note,created_at,reviewed_at,publication_blocked"
    ).order("created_at", {ascending:false}).limit(MAX_VISIBLE);
    if (result.error) {
        banner("Could not read the review queue: " + result.error.message, "info");
        get("refreshReviews").disabled = false;
        return;
    }
    entries = Array.isArray(result.data) ? result.data : [];
    const publicResult = await client.from("forge_public_mods")
        .select("submission_id,id,download_url,license,published_at")
        .order("published_at", {ascending:false}).limit(500);
    publishedReady = !publicResult.error;
    published = new Map();
    if (publishedReady) {
        for (const entry of publicResult.data || []) published.set(entry.submission_id, entry);
    } else {
        banner("Public release status unavailable; Publish is disabled until it reconnects.", "info");
    }
    creators = new Map();
    const ids = [...new Set(entries.map(mod => mod.owner_id).filter(Boolean))];
    if (ids.length) {
        const profiles = await client.from("creator_profiles")
            .select("user_id,github_login,display_name").in("user_id", ids);
        if (!profiles.error && Array.isArray(profiles.data)) {
            for (const user of profiles.data) creators.set(user.user_id, user);
        }
    }
    get("refreshReviews").disabled = false;
    get("queueLimitNote").textContent = "Showing the latest " + MAX_VISIBLE +
        " private requests. Approve is review-only; Publish requires a separate " +
        "rights declaration and uploads an approved ZIP to the public catalog.";
    render();
    if (isOwner) await loadModeratorAudit(true);
}

async function initialize() {
    let config;
    try {
        const response = await fetch("./creator-config.json", {cache:"no-store"});
        if (!response.ok) throw new Error("Creator configuration unavailable");
        config = await response.json();
        if (config.oauth_enabled !== true || !String(config.supabase_url).startsWith("https://") ||
            !String(config.supabase_url).endsWith(".supabase.co") ||
            !String(config.supabase_publishable_key).startsWith("sb_publishable_")) {
            throw new Error("Creator authentication is not configured.");
        }
        publicApiKey = config.supabase_publishable_key;
        const sdk = await import("https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.50.0/+esm");
        client = sdk.createClient(config.supabase_url, config.supabase_publishable_key, {
            auth:{flowType:"pkce",detectSessionInUrl:true,persistSession:true}
        });
        const session = await client.auth.getSession();
        if (session.error) throw session.error;
        currentUser = session.data.session && session.data.session.user;
        if (!currentUser) {
            showSignIn("Sign in on Creator Studio, then return to this page.");
            return;
        }
        const access = await client.rpc("forge_is_moderator");
        if (access.error) throw access.error;
        if (access.data !== true) {
            showSignIn("This GitHub account is not authorized to moderate Forge Hub.", true);
            return;
        }
        const ownerResult = await client.rpc("forge_is_owner");
        if (ownerResult.error) throw ownerResult.error;
        isOwner = ownerResult.data === true;
        get("moderatorAdminPanel").classList.toggle("hidden", !isOwner);
        get("moderatorAuditPanel").classList.toggle("hidden", !isOwner);
        get("signInHelp").classList.add("hidden");
        get("reviewPanel").classList.remove("hidden");
        banner(isOwner ?
            "Owner verified. You can manage moderators and review Forge Hub submissions." :
            "Moderator verified. You can review submissions but cannot manage moderator accounts.", "ok");
        if (isOwner) await loadModeratorRoster();
        await loadQueue();
    } catch (error) {
        showSignIn("Moderator login check failed: " + (error.message || String(error)));
    }
}

for (const filter of document.querySelectorAll(".mod-filter")) {
    filter.addEventListener("click", () => {
        activeFilter = filter.dataset.state || "all";
        for (const button of document.querySelectorAll(".mod-filter")) {
            const active = button.dataset.state === activeFilter;
            button.classList.toggle("active", active);
            button.setAttribute("aria-pressed", String(active));
        }
        render();
    });
}
get("moderatorAddForm").addEventListener("submit", event => {
    event.preventDefault();
    const login = get("moderatorGithub").value.trim().replace(/^@/, "");
    if (!safeGithubLogin(login)) {
        get("moderatorAdminMessage").textContent = "Enter a valid GitHub username.";
        return;
    }
    manageModerator(login, "grant", get("moderatorAddButton"));
});
get("moderatorAuditFilter").addEventListener("change", renderModeratorAudit);
get("moderatorAuditSearch").addEventListener("input", renderModeratorAudit);
get("moderatorAuditRefresh").addEventListener("click", () => loadModeratorAudit(true));
get("moderatorAuditMore").addEventListener("click", () => loadModeratorAudit(false));
get("reviewSearch").addEventListener("input", render);
get("refreshReviews").addEventListener("click", loadQueue);
initialize();
