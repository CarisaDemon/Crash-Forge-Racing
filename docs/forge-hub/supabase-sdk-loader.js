/* Pinned Forge Hub browser auth SDK, with a verified fallback CDN. */
"use strict";

const SDK_SOURCES = [
  "https://esm.sh/@supabase/supabase-js@2.50.0?bundle",
  "https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.50.0/+esm"
];

export async function loadSupabaseCreateClient() {
  const failures = [];
  for (const url of SDK_SOURCES) {
    try {
      const sdk = await import(url);
      const createClient =
        typeof sdk.createClient === "function" ? sdk.createClient :
        typeof sdk.default?.createClient === "function" ? sdk.default.createClient : null;
      if (createClient) return createClient;
      failures.push(new URL(url).hostname + ": createClient export missing");
    } catch (error) {
      failures.push(new URL(url).hostname + ": " +
                    String(error?.message || error).slice(0, 120));
    }
  }
  // No credentials or auth tokens are included in this diagnostic.
  console.error("[Forge Hub] Supabase SDK load failed: " + failures.join(" | "));
  throw Error("The account library could not be loaded. Check your connection and reload the page.");
}
