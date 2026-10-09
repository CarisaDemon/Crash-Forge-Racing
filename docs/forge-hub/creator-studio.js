/* Forge Hub Creator Studio.
   GitHub OAuth runs on GitHub/Supabase, NEVER in a form on this site.
   In unconfigured mode, local text-only drafts work. No fake sign-in.
   All ownership, status and private Storage access is enforced in SQL RLS. */
"use strict";

const el = id => document.getElementById(id);
const make = (tag, cls, text) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text !== undefined) n.textContent = String(text);
    return n;
};
const DRAFT_KEY = "cfr.forge-hub.creator-draft.v1";
const MAX_FILE_BYTES = 50 * 1024 * 1024;
const MOD_META = window.ForgeModMeta;
if (!MOD_META) throw new Error("ForgeModMeta script must load before Creator Studio.");
let client = null;
let activeUser = null;
let activeProfile = null;
let activeConfig = null;
let packageInfo = null;
let submitting = false;
let profileSaving = false;
let editInProgress = false;

const safeGithubHandle = name =>
    /^[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,37}[a-zA-Z0-9])?$/.test(String(name || ""));

function notice(message, kind) {
    const node = el("backendBanner");
    node.textContent = message;
    node.className = "notice " + (kind || "info");
}
function message(text) {
    el("formMessage").textContent = text;
}
function setFileInfo(text, kind, hashText) {
    el("fileStatus").textContent = kind || "CHECK";
    el("fileDetails").textContent = text;
    el("fileHash").textContent = hashText || "";
}
function syncControls() {
    const connected = !!(client && activeUser && activeProfile);
    el("connectBtn").disabled = !client || !!activeUser;
    el("connectBtn").textContent = client ? "CONNECT VIA GITHUB" : "ACCOUNT LOGIN PENDING";
    el("connectBtn").classList.toggle("hidden", !!activeUser);
    el("disconnectBtn").classList.toggle("hidden", !activeUser);
    el("saveProfileBtn").disabled = !connected || profileSaving;
    el("displayName").disabled = !connected;
    el("biography").disabled = !connected;
    el("refreshBtn").disabled = !connected;
    el("submitBtn").disabled = !connected || submitting || !packageInfo;
}
function refreshCategoryFields() {
    const category = el("modType").value;
    for (const [id, kind] of [["racerMetadata", "Character"],
                              ["kartMetadata", "Kart"],
                              ["mapMetadata", "Track"],
                              ["wheelMetadata", "Wheels"]]) {
        el(id).classList.toggle("hidden", category !== kind);
    }
    const showWheels = category === "Kart" && el("kartDrive").value === "Wheeled";
    el("wheelSetupWrap").classList.toggle("hidden", !showWheels);
    el("wheelSetup").disabled = !showWheels;
    el("racerClass").disabled = category !== "Character";
    el("mapKind").disabled = category !== "Track";
    el("kartDrive").disabled = category !== "Kart";
}
function detailsFromForm(strict = false) {
    const category = el("modType").value;
    return MOD_META.normalize(category, {
        map_kind: el("mapKind").value,
        racer_class: el("racerClass").value,
        kart_drive: el("kartDrive").value,
        wheel_setup: el("wheelSetup").value
    }, strict);
}
function applyTypeDetails(item) {
    const kind = item.category;
    const details = MOD_META.normalize(kind, item, false);
    if (details.map_kind) el("mapKind").value = details.map_kind;
    if (details.racer_class) el("racerClass").value = details.racer_class;
    if (details.kart_drive) el("kartDrive").value = details.kart_drive;
    if (details.wheel_setup) el("wheelSetup").value = details.wheel_setup;
    refreshCategoryFields();
}

function loadLocalDraft() {
    let raw = null;
    try { raw = JSON.parse(localStorage.getItem(DRAFT_KEY) || "null"); } catch {}
    if (!raw || typeof raw !== "object") return null;
    const fields = {
        title: String(raw.title || "").slice(0, 100),
        category: MOD_META.categories.includes(raw.category) ? raw.category : "Character",
        version: String(raw.version || "1.0").slice(0, 28),
        description: String(raw.description || "").slice(0, 3000),
        ...MOD_META.normalize(MOD_META.categories.includes(raw.category) ?
            raw.category : "Character", raw, false)
    };
    return fields.title ? fields : null;
}
function currentDraft() {
    return {
        title: el("modTitle").value.trim(),
        category: el("modType").value,
        version: el("modVersion").value.trim(),
        description: el("modDescription").value.trim(),
        ...detailsFromForm()
    };
}
function saveDraft() {
    const draft = currentDraft();
    if (!draft.title) {
        message("Enter a mod title before saving a local draft.");
        return;
    }
    try {
        localStorage.setItem(DRAFT_KEY, JSON.stringify(draft));
        message("Local draft saved on this browser (text only). No ZIP was uploaded.");
        renderLocalDraft();
    } catch {
        message("This browser could not save a local draft.");
    }
}
function clearDraft() {
    try { localStorage.removeItem(DRAFT_KEY); } catch {}
    el("modForm").reset();
    el("modVersion").value = "1.0";
    refreshCategoryFields();
    packageInfo = null;
    setFileInfo("Choose a ZIP to calculate SHA-256 locally.", "NO FILE");
    syncControls();
    renderLocalDraft();
    message("Local draft cleared. No remote file was changed.");
}
function applyDraft(draft) {
    if (!draft) return;
    el("modTitle").value = draft.title;
    el("modType").value = draft.category;
    el("modVersion").value = draft.version;
    el("modDescription").value = draft.description;
    applyTypeDetails(draft);
}
function renderLocalDraft() {
    const old = el("myModsList").querySelector("[data-local-draft]");
    if (old) old.remove();
    const draft = loadLocalDraft();
    if (!draft) return;
    const row = make("div", "mod-row");
    row.dataset.localDraft = "1";
    const body = make("div");
    body.append(make("h3", "", draft.title), make("p", "", draft.category + " / v" + draft.version));
    body.append(make("p", "mod-type-detail", MOD_META.summary(draft.category, draft)));
    const action = make("button", "btn ghost", "EDIT DRAFT");
    action.type = "button";
    action.addEventListener("click", () => {
        applyDraft(draft);
        el("upload").scrollIntoView({behavior: "smooth", block: "start"});
    });
    row.append(body, action);
    const state = make("span", "status-chip", "LOCAL / NOT SUBMITTED");
    body.append(state);
    el("myModsList").prepend(row);
}
function showOfflineMods() {
    el("myModsList").replaceChildren(
        make("div", "empty", "Private ZIP uploads are not connected yet. Use MY GITHUB SUBMISSIONS for requests sent through GitHub, or save a local draft.")
    );
    el("myModsLabel").textContent = "LOCAL PREPARATION";
    renderLocalDraft();
}
function showProfile() {
    const handle = activeProfile && activeProfile.github_login;
    el("creatorName").textContent = activeProfile ? activeProfile.display_name : "Guest creator";
    el("creatorHandle").textContent = handle ? "@" + handle : "GitHub account not linked";
    el("identityStatus").textContent = activeProfile ? "GITHUB LINKED" : "OFFLINE";
    el("identityStatus").className = activeProfile ? "status-chip verified" : "status-chip";
    el("displayName").value = activeProfile ? activeProfile.display_name : "";
    el("biography").value = activeProfile ? activeProfile.biography || "" : "";
    const actions = el("profileLinks");
    actions.replaceChildren();
    if (handle && safeGithubHandle(handle)) {
        const profile = make("a", "btn secondary", "PUBLIC PROFILE");
        profile.href = "./creator.html?user=" + encodeURIComponent(handle);
        const github = make("a", "btn ghost", "VIEW GITHUB");
        github.href = "https://github.com/" + encodeURIComponent(handle);
        github.rel = "noopener noreferrer";
        github.target = "_blank";
        actions.append(profile, github);
    }
    const currentAvatar = el("creatorAvatar");
    currentAvatar.replaceChildren();
    currentAvatar.textContent = activeProfile ? "GH" : "CF";
    const url = activeProfile && activeProfile.avatar_url;
    try {
        const parsed = new URL(url);
        if (parsed.protocol === "https:" && parsed.hostname === "avatars.githubusercontent.com") {
            const img = make("img", "avatar");
            img.src = parsed.href; img.alt = "GitHub creator avatar";
            img.referrerPolicy = "no-referrer";
            currentAvatar.replaceChildren(img);
        }
    } catch {}
    syncControls();
}

async function checkPackage() {
    const file = el("modPackage").files[0];
    packageInfo = null;
    syncControls();
    if (!file) {
        setFileInfo("Select a ZIP to calculate SHA-256 locally.", "NO FILE");
        return;
    }
    if (!/\.zip$/i.test(file.name)) {
        setFileInfo("Unsupported file: a .zip package is required.", "REJECTED");
        return;
    }
    if (file.size <= 0 || file.size > MAX_FILE_BYTES) {
        setFileInfo("The ZIP must be between 1 byte and 50 MiB.", "REJECTED");
        return;
    }
    if (!globalThis.crypto || !crypto.subtle) {
        setFileInfo("Secure browser context required for SHA-256. Use HTTPS or localhost.", "BLOCKED");
        return;
    }
    try {
        setFileInfo("Reading ZIP and computing SHA-256...", "HASHING");
        const head = new Uint8Array(await file.slice(0,4).arrayBuffer());
        if (!(head[0]===0x50 && head[1]===0x4b && head[2]===0x03 && head[3]===0x04)) {
            throw new Error("ZIP header invalid. Repackage your mod as a standard ZIP.");
        }
        const buffer = await file.arrayBuffer();
        const digest = await crypto.subtle.digest("SHA-256", buffer);
        const hash = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2,"0")).join("");
        if (el("modPackage").files[0] !== file) return;
        packageInfo = {file, hash};
        setFileInfo(file.name + " - " + (file.size / 1048576).toFixed(2) + " MiB",
                    "READY", "SHA-256: " + hash);
    } catch (err) {
        packageInfo = null;
        setFileInfo(String(err.message || err), "REJECTED");
    }
    syncControls();
}
function addModRow(item) {
    const row = make("article", "mod-row");
    const info = make("div");
    info.append(make("h3", "", item.title));
    info.append(make("p", "", item.category + " / v" + item.mod_version +
        " / " + (item.created_at ? new Date(item.created_at).toLocaleDateString() : "")));
    info.append(make("p", "mod-type-detail", MOD_META.summary(item.category, item)));
    if (item.moderator_note) info.append(make("p", "", "Review note: " + item.moderator_note));
    const status = make("span", "status-chip " +
        (item.status === "pending" ? "pending" : item.status === "rejected" ? "rejected" : "verified"),
        String(item.status || "unknown").toUpperCase());
    info.append(status);
    row.append(info);
    if (item.status === "pending") {
        const controls = make("div");
        const edit = make("button", "btn ghost", "EDIT DETAILS");
        edit.type = "button";
        edit.addEventListener("click", () => showSubmissionEditor(row, item));
        controls.append(edit);
        row.append(controls);
    } else if (item.status === "approved") {
        const newVersion = make("button", "btn ghost", "NEW VERSION");
        newVersion.type = "button";
        newVersion.addEventListener("click", () => {
            el("modTitle").value = item.title;
            el("modType").value = item.category;
            el("modDescription").value = item.description;
            el("modVersion").value = item.mod_version;
            applyTypeDetails({...item, category:item.category});
            message("Prepare a new ZIP and increase the version before re-submitting for review.");
            el("upload").scrollIntoView({behavior:"smooth"});
        });
        row.append(newVersion);
    }
    return row;
}
function showSubmissionEditor(row, item) {
    if (editInProgress) return;
    editInProgress = true;
    const editor = make("div", "inset");
    editor.style.marginTop = "12px";
    const title = make("input"); title.value = item.title; title.maxLength=100;
    const version = make("input"); version.value = item.mod_version; version.maxLength=28;
    const desc = make("textarea"); desc.value=item.description; desc.maxLength=3000;
    const patchMeta = {};
    const fieldInputs = {};
    const metaSchema = item.category === "Track" ?
        [["map_kind", "Map type", MOD_META.maps]] :
        item.category === "Character" ?
        [["racer_class", "Stats class", MOD_META.classes]] :
        item.category === "Kart" ?
        [["kart_drive", "Kart design", MOD_META.drives],
         ["wheel_setup", "Wheel setup (Wheeled only)", MOD_META.wheels]] : [];
    for (const [key, label, options] of metaSchema) {
        const line = make("div", "field");
        const selector = make("select");
        for (const value of options) {
            const option = make("option", "", value);
            option.value = value;
            selector.append(option);
        }
        selector.value = options.includes(item[key]) ? item[key] : options[0];
        fieldInputs[key] = selector;
        line.append(make("label", "", label), selector);
        editor.append(line);
    }
    for (const [text, input] of [["Name",title],["Version",version],["Description",desc]]) {
        const line = make("div", "field");
        line.append(make("label", "", text), input); editor.append(line);
    }
    const bar = make("div", "form-actions");
    const save = make("button", "btn secondary", "SAVE CHANGES");
    const cancel = make("button", "btn ghost", "CANCEL");
    save.type=cancel.type="button";
    const dismiss = () => {editor.remove(); editInProgress=false;};
    cancel.addEventListener("click", dismiss);
    save.addEventListener("click", async () => {
        if (title.value.trim().length < 3 || version.value.trim().length < 1 ||
            desc.value.trim().length < 10) {
            message("Please provide a name, version and description of at least 10 characters.");
            return;
        }
        let updatedMeta;
        try {
            const changed = {};
            for (const key of MOD_META.detailKeys)
                changed[key] = fieldInputs[key] ? fieldInputs[key].value : null;
            updatedMeta = MOD_META.normalize(item.category, changed, true);
        } catch (err) {
            message(String(err.message || err));
            return;
        }
        save.disabled = true;
        const result = await client.from("mod_submissions").update({
            title:title.value.trim(),mod_version:version.value.trim(),
            description:desc.value.trim(),...updatedMeta
        }).eq("id",item.id).eq("status","pending").select("id");
        if (result.error || !result.data || result.data.length !== 1) {
            message("Update failed: " + (result.error && result.error.message || "Submission is no longer pending."));
            save.disabled = false; return;
        }
        dismiss(); message("Pending submission details updated.");
        await loadMyMods();
    });
    bar.append(save,cancel);editor.append(bar);
    row.after(editor);
}
async function loadMyMods() {
    if (!client || !activeUser || !activeProfile) {showOfflineMods();return;}
    el("myModsLabel").textContent = "LOADING";
    const result = await client.from("mod_submissions")
        .select("id,title,category,mod_version,description,map_kind,racer_class,kart_drive,wheel_setup,status,moderator_note,created_at")
        .order("created_at", {ascending:false}).limit(50);
    if (result.error) {
        el("myModsList").replaceChildren(make("div","empty","Failed to load private submissions: "+result.error.message));
        renderLocalDraft(); return;
    }
    const target = el("myModsList");
    target.replaceChildren();
    for (const item of result.data || []) target.append(addModRow(item));
    if (!result.data || !result.data.length) {
        target.append(make("div","empty","No private submissions yet. Your first mod can be submitted below."));
    }
    el("myModsLabel").textContent = String((result.data || []).length) + " SUBMISSIONS";
    renderLocalDraft();
}
async function saveProfile(event) {
    event.preventDefault();
    if (!client || !activeUser || !activeProfile || profileSaving) return;
    const name = el("displayName").value.trim().slice(0,60);
    const biography = el("biography").value.trim().slice(0,500);
    if (!name) {
        el("profileMessage").textContent = "Display name cannot be empty.";
        return;
    }
    profileSaving = true;
    syncControls();
    try {
        const {data,error} = await client.from("creator_profiles")
            .update({display_name:name,biography})
            .eq("user_id",activeUser.id)
            .select("user_id,github_id,github_login,display_name,biography,avatar_url").single();
        if (error) throw error;
        activeProfile = data;
        showProfile();
        el("profileMessage").textContent = "Profile saved. GitHub account ownership is unchanged.";
    } catch (err) {
        el("profileMessage").textContent = "Could not save profile: " + String(err.message || err);
    } finally {
        profileSaving = false;
        syncControls();
    }
}
function normalizedZipFile(file) {
    // Windows often reports ZIPs as application/x-zip-compressed. Storage may
    // use the File MIME from multipart/form-data, ignoring contentType options.
    // Re-wrap the same bytes with the canonical ZIP MIME; keep the filename.
    const zip = new File([file], file.name, {
        type:"application/zip", lastModified:file.lastModified
    });
    if (zip.size !== file.size) throw new Error("The ZIP changed size during upload preparation.");
    return zip;
}
async function submitMod(event) {
    event.preventDefault();
    if (submitting || !client || !activeUser || !activeProfile) {
        message("Connect a GitHub creator account to submit a ZIP.");
        return;
    }
    const form = el("modForm");
    if (!form.reportValidity() || !el("rightsConsent").checked || !packageInfo) {
        message("Fill in all fields, confirm content rights and select a verified ZIP.");
        return;
    }
    const draft = currentDraft();
    if (draft.description.length < 10 || draft.title.length < 3 ||
        !MOD_META.categories.includes(draft.category)) {
        message("Invalid mod title, category or description.");
        return;
    }
    let validatedMeta;
    try {
        validatedMeta = detailsFromForm(true);
    } catch (err) {
        message(String(err.message || err));
        return;
    }
    submitting = true;
    syncControls();
    const uuid = crypto.randomUUID();
    const zipPath = activeUser.id + "/" + uuid + ".zip";
    let zipUploaded = false;
    try {
        message("Uploading private ZIP for moderation. No public listing will be created yet...");
        // The upload body must also be application/zip. On Windows the selected
        // File usually has type application/x-zip-compressed, which some Storage
        // multipart encoders send instead of the explicit contentType option.
        const zipBody = normalizedZipFile(packageInfo.file);
        const uploaded = await client.storage.from("forge-mod-queue").upload(
            zipPath, zipBody, {
                cacheControl: "0", upsert:false, contentType:"application/zip"
            }
        );
        if (uploaded.error) throw uploaded.error;
        zipUploaded = true;
        const recorded = await client.from("mod_submissions").insert({
            title:draft.title,
            category:draft.category,
            mod_version:draft.version,
            description:draft.description,
            ...validatedMeta,
            zip_path:zipPath,
            original_filename:packageInfo.file.name.slice(0,180),
            sha256:packageInfo.hash,
            file_size_bytes:packageInfo.file.size
        }).select("id").single();
        if (recorded.error) throw recorded.error;
        message("Submission saved privately! Status: PENDING REVIEW. Publication requires moderator approval.");
        try {localStorage.removeItem(DRAFT_KEY);} catch {}
        form.reset();
        el("modVersion").value="1.0";
        refreshCategoryFields();
        packageInfo=null;
        setFileInfo("Choose another ZIP to start a new submission.","NO FILE");
        await loadMyMods();
    } catch (err) {
        if (zipUploaded) {
            // Cleanup of orphan uploads is permitted by the storage DELETE policy,
            // but only if no submission references that exact object.
            const removed = await client.storage.from("forge-mod-queue").remove([zipPath]);
            if (removed.error) {
                message("Submit failed: "+String(err.message||err)+". An unlisted ZIP may remain in your private review area. Contact a moderator.");
            } else {
                message("Submit failed: "+String(err.message||err)+". Uploaded ZIP was cleaned up.");
            }
        } else message("Upload failed: "+String(err.message||err));
    } finally {
        submitting = false;
        syncControls();
    }
}
async function updateModerationNavigation() {
    const link = el("moderationLink");
    link.classList.add("hidden");
    if (!client || !activeUser) return;
    try {
        // The Supabase RPC checks the signed-in user's verified GitHub
        // provider identity. Hiding this link alone is NOT an access control.
        const result = await client.rpc("forge_is_moderator");
        if (!result.error && result.data === true) {
            link.classList.remove("hidden");
        }
    } catch { /* The private moderation link stays hidden. */ }
}
async function refreshSession(session) {
    const previousUserId = activeUser && activeUser.id;
    activeUser = session && session.user || null;
    activeProfile = null;
    el("moderationLink").classList.add("hidden");
    if (!activeUser) {
        showProfile();
        showOfflineMods();
        if (client) notice("Creator backend is available. Connect with GitHub to submit and manage mods.", "ok");
        return;
    }
    notice("Checking your signed-in GitHub identity...");
    try {
        const result = await client.rpc("sync_my_github_profile");
        if (result.error) throw result.error;
        if (!result.data || !safeGithubHandle(result.data.github_login)) {
            throw new Error("A linked GitHub account is required for this creator studio.");
        }
        activeProfile = result.data;
        showProfile();
        notice("GitHub identity linked. Your submissions remain private until moderator approval.", "ok");
        await loadMyMods();
        await updateModerationNavigation();
    } catch (err) {
        notice("Could not verify your GitHub creator identity: " + String(err.message || err), "info");
        showProfile();
        showOfflineMods();
    }
}
async function setupGitHubSubmissions() {
    try {
        const response = await fetch("./config.json", {cache:"no-store"});
        if (!response.ok) return;
        const settings = await response.json();
        const url = new URL(String(settings.submit_url || ""));
        if (url.protocol !== "https:" || url.hostname !== "github.com" ||
            !/^\/[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+\/issues\/new$/.test(url.pathname)) return;
        const submit = el("githubSubmitLink");
        submit.href = url.href;
        submit.classList.remove("hidden");
        const repository = url.pathname.split("/").slice(1,3).join("/");
        const my = el("githubMyModsLink");
        my.href = "https://github.com/" + repository +
            "/issues?q=" + encodeURIComponent('is:issue author:@me "[MOD]"');
        my.classList.remove("hidden");
    } catch { /* No public submission provider is configured yet. */ }
}
async function initialize() {
    showOfflineMods();
    setupGitHubSubmissions();
    applyDraft(loadLocalDraft());
    syncControls();
    let settings = null;
    try {
        const response = await fetch("./creator-config.json", {cache:"no-store"});
        if (!response.ok) throw new Error("Settings not available");
        settings = await response.json();
    } catch {
        notice("Creator accounts are not configured. You can still prepare and save a local draft.");
        return;
    }
    const url = String(settings.supabase_url || "").trim();
    const key = String(settings.supabase_publishable_key || "").trim();
    const provider = String(settings.auth_provider || "github");
    if (!url || !key || settings.oauth_enabled !== true) {
        notice("Forge Hub database and private storage are ready. GitHub OAuth is not enabled yet; continue using local drafts or verified GitHub Issue submissions.");
        return;
    }
    let endpoint = null;
    try {
        endpoint = new URL(url);
        if (endpoint.protocol !== "https:" || !endpoint.hostname.endsWith(".supabase.co") ||
            !key || key.startsWith("sb_secret_") || key.toLowerCase().includes("service_role") ||
            provider !== "github") throw new Error("Public account settings are invalid.");
    } catch {
        notice("Creator account configuration is invalid. Never put a secret key in public website settings.");
        return;
    }
    try {
        // Official browser client; only loaded for an explicitly configured backend.
        const sdk = await import("https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.50.0/+esm");
        client = sdk.createClient(url, key, {
            auth:{flowType:"pkce",detectSessionInUrl:true,persistSession:true}
        });
        const session = await client.auth.getSession();
        await refreshSession(session.data.session);
        client.auth.onAuthStateChange((_event, value) => {
            // Avoid calling Supabase async operations synchronously inside an auth callback.
            window.setTimeout(() => refreshSession(value), 0);
        });
        syncControls();
    } catch (err) {
        client = null;
        notice("Creator account service unavailable: "+String(err.message||err));
        syncControls();
    }
}

el("connectBtn").addEventListener("click", async () => {
    if (!client) return;
    const target = new URL("./studio.html", location.href).href;
    const result = await client.auth.signInWithOAuth({
        provider:"github", options:{redirectTo:target}
    });
    if (result.error) notice("Could not open GitHub authorization: "+result.error.message);
});
el("disconnectBtn").addEventListener("click", async () => {
    if (!client) return;
    const result = await client.auth.signOut();
    if (result.error) notice("Sign-out failed: "+result.error.message);
    else await refreshSession(null);
});
el("refreshBtn").addEventListener("click",loadMyMods);
el("saveProfileBtn").closest("form").addEventListener("submit",saveProfile);
el("modPackage").addEventListener("change",checkPackage);
el("modType").addEventListener("change",refreshCategoryFields);
el("racerClass").addEventListener("change",refreshCategoryFields);
el("kartDrive").addEventListener("change",refreshCategoryFields);
el("modForm").addEventListener("submit",submitMod);
el("draftBtn").addEventListener("click",saveDraft);
el("clearBtn").addEventListener("click",clearDraft);
refreshCategoryFields();
initialize();
