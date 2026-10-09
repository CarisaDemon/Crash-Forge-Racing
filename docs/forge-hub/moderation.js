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
        note.placeholder = "Add a moderator note. A reason is required for rejection.";
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
    appendPublicationControls(card, item);
    return card;
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
        card.append(section);
        return;
    }
    if (item.status !== "approved") return;
    if (item.publication_blocked) {
        const warning = node("div", "moderation-publication-blocked");
        warning.append(node("strong", "", "PUBLICATION BLOCKED"));
        warning.append(node("p", "", "Third-party source assets do not have documented redistribution clearance. This ZIP stays private, even though its review status is Approved."));
        card.append(warning);
        return;
    }
    if (!publishedReady) {
        card.append(node("p", "caption", "Publication status unavailable. Retry when the public catalog reconnects."));
        return;
    }
    const section = node("div", "moderation-publish");
    section.append(node("h4", "", "PUBLIC RELEASE — SEPARATE FROM APPROVAL"));
    section.append(node("p", "caption",
        "Publishing makes the ZIP downloadable by everyone. Publish only original creations or content with explicit written redistribution permission."));
    const basisLabel = node("label", "moderation-field");
    basisLabel.append(node("span", "meta", "RIGHTS EVIDENCE (40–1000 CHARACTERS)"));
    const basis = node("textarea");
    basis.minLength = 40;
    basis.maxLength = 1000;
    basis.placeholder = "Describe who created these assets and your evidence of their permission to distribute this ZIP publicly.";
    basisLabel.append(basis);
    section.append(basisLabel);

    const licenseLabel = node("label", "moderation-field");
    licenseLabel.append(node("span", "meta", "PUBLIC RELEASE LICENSE"));
    const license = node("select", "moderation-license");
    for (const [value,text] of [
        ["All rights reserved", "All rights reserved — distribution permission verified"],
        ["CC-BY-4.0", "Creative Commons Attribution 4.0"],
        ["CC0-1.0", "Creative Commons Zero 1.0"]
    ]) {
        const option = node("option", "", text);
        option.value = value;
        license.append(option);
    }
    licenseLabel.append(license);
    section.append(licenseLabel);

    const confirmation = node("label", "checkbox-row");
    const checkbox = node("input");
    checkbox.type = "checkbox";
    confirmation.append(checkbox, document.createTextNode(
        " I verified ownership/redistribution rights and understand this uploads a permanently public downloadable ZIP."));
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
async function publishSubmission(item, evidence, license, confirmed, button, feedback) {
    if (!client || !currentUser || busy || !publishedReady || published.has(item.id)) return;
    const basis = String(evidence || "").trim();
    if (item.status !== "approved" || item.publication_blocked) {
        feedback.textContent = "Only approved, distribution-cleared requests can be published.";
        return;
    }
    if (!confirmed || basis.length < 40 || basis.length > 1000) {
        feedback.textContent = "Confirm rights and provide 40–1000 characters of evidence before publishing.";
        return;
    }
    if (!window.confirm("This action releases the ZIP to the PUBLIC INTERNET and lists it in Forge Hub and creator profiles. Continue only if you have verified distribution rights?")) return;
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
                submission_id: item.id, rights_basis: basis,
                license, rights_confirmed: true
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
    const confirmText = decision === "approved" ?
        "Approve this PRIVATE request? You must have verified its rights. This does NOT publish the ZIP or add it to the public catalog." :
        "Reject this private submission? The creator will see your review note.";
    if (!window.confirm(confirmText)) return;
    busy = true;
    for (const button of buttons) button.disabled = true;
    message.textContent = "Saving " + decision + " decision...";
    try {
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
        get("signInHelp").classList.add("hidden");
        get("reviewPanel").classList.remove("hidden");
        banner("Verified GitHub moderator. ZIP files are private and review decisions are logged.", "ok");
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
get("reviewSearch").addEventListener("input", render);
get("refreshReviews").addEventListener("click", loadQueue);
initialize();
