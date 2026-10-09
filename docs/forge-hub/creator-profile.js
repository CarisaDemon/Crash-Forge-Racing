/* Public creator page; GitHub owns login. Curated catalog entries only. */
"use strict";
const byId = id => document.getElementById(id);
const make = (tag, className, value) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (value !== undefined) node.textContent = String(value);
    return node;
};
const creatorHandle = new URLSearchParams(location.search).get("user") || "";
const loginValid = /^[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?$/.test(creatorHandle);
const safeHttps = value => {
    try {
        const url = new URL(String(value || ""));
        return url.protocol === "https:" && !url.username && !url.password ? url.href : "";
    } catch { return ""; }
};
const setBanner = (message, good = false) => {
    const banner = byId("creatorBanner");
    banner.textContent = message;
    banner.className = "notice " + (good ? "ok" : "info");
};
async function getJson(url) {
    const response = await fetch(url, {cache: "no-store"});
    if (!response.ok) throw new Error("HTTP " + response.status);
    return response.json();
}
function drawPublicMods(mods) {
    const target = byId("approvedMods");
    target.replaceChildren();
    byId("approvedCount").textContent = String(mods.length) + " PUBLISHED";
    if (!mods.length) {
        target.append(make("div", "empty", "No published mods are listed for this creator yet."));
        return;
    }
    for (const mod of mods) {
        const card = make("article", "creator-card");
        card.append(make("span", "kind", ({Character:"CHARACTER",Kart:"KART",Wheels:"WHEELS",Track:"MAP / TRACK"})[mod.type] || "MOD"));
        card.append(make("h3", "", mod.title || "Untitled"));
        card.append(make("p", "", mod.description || ""));
        card.append(make("p", "mod-type-detail", window.ForgeModMeta.summary(mod.type, mod)));
        const row = make("div", "small-actions");
        const detail = safeHttps(mod.page_url);
        const download = safeHttps(mod.download_url);
        if (detail) {
            const anchor = make("a", "small-link", "DETAILS");
            anchor.href = detail; anchor.rel = "noopener noreferrer"; anchor.target = "_blank";
            row.append(anchor);
        }
        if (download && /^[a-f0-9]{64}$/i.test(String(mod.sha256 || ""))) {
            const anchor = make("a", "small-link", "VERIFIED ZIP");
            anchor.href = download; anchor.rel = "noopener noreferrer"; anchor.target = "_blank";
            row.append(anchor);
        }
        card.append(row); target.append(card);
    }
}
async function loadCreator() {
    if (!loginValid) {
        setBanner("Invalid creator name. Open a creator profile from an approved Forge Hub mod.");
        drawPublicMods([]);
        return;
    }
    const normalized = creatorHandle.toLowerCase();
    byId("publicGithubName").textContent = "@" + creatorHandle;
    byId("publicDisplayName").textContent = creatorHandle;
    const link = make("a", "btn secondary", "VIEW ON GITHUB");
    link.href = "https://github.com/" + encodeURIComponent(creatorHandle);
    link.rel = "noopener noreferrer"; link.target = "_blank";
    byId("publicCreatorActions").append(link);
    let github = null;
    try {
        github = await getJson("https://api.github.com/users/" + encodeURIComponent(creatorHandle));
        if (!github || github.type !== "User") throw new Error("GitHub account not found");
        if (String(github.login || "").toLowerCase() !== normalized) throw new Error("GitHub profile mismatch");
        byId("publicDisplayName").textContent = github.name || github.login;
        byId("publicGithubName").textContent = "@" + github.login;
        byId("publicCreatorBio").textContent = github.bio || "GitHub creator profile. See approved mods below.";
        const avatar = safeHttps(github.avatar_url);
        if (avatar && new URL(avatar).hostname === "avatars.githubusercontent.com") {
            const img = make("img", "avatar");
            img.src = avatar; img.alt = github.login + " avatar";
            img.referrerPolicy = "no-referrer";
            byId("publicAvatar").replaceWith(img);
            img.id = "publicAvatar";
        }
        setBanner("Public GitHub profile confirmed. Only officially published Forge Hub mods appear here.", true);
    } catch {
        setBanner("GitHub profile information is unavailable. Showing approved catalog entries if present.");
        byId("publicCreatorBio").textContent = "Public GitHub profile is not available right now.";
    }
    try {
        const data = await getJson("https://mjvpkerobjgoldmimyxz.supabase.co/functions/v1/forge-publications")
            .catch(() => getJson("./catalog.json"));
        const mods = Array.isArray(data.mods) ? data.mods : [];
        const approved = mods.filter(mod => {
            if (!mod || mod.compatibility !== "cfr") return false;
            if (!window.ForgeModMeta.categories.includes(mod.type)) return false;
            const matchingLogin = String(mod.author_github_login || "").toLowerCase() === normalized;
            const matchingId = github && Number(mod.author_github_id) === Number(github.id);
            return matchingLogin || matchingId;
        });
        drawPublicMods(approved);
    } catch {
        drawPublicMods([]);
        byId("approvedMods").prepend(make("p", "muted", "Could not load the public catalog."));
    }
}
loadCreator();
