# Forge Hub Mod Packager (Web)

Creator Studio: https://carisademon.github.io/Crash-Forge-Racing/forge-hub/studio.html

1. Select Character, Kart, Wheels, or Track in the submission form.
2. Enter the mod name, version, description and category details.
3. Choose your native mod folder or the individual mod files.
4. Click BUILD & ATTACH ZIP. All files are inspected and packaged in your browser.
5. Optionally click DOWNLOAD ZIP to keep a local copy.
6. Sign in with GitHub and click SUBMIT FOR REVIEW to send the package to the private review queue.

Already have a valid ZIP? Expand ADVANCED IMPORT to use the original
file uploader. No mod is published without review.

ZIP content is in a single readable mod folder, with a valid native INI and
forge_package.json; the upload step assigns the permanent Forge Hub mod ID
inside that same ZIP. Submitting a new version retains the published ID.

- Characters/karts/wheels: an OBJ model (converted using Model Importer).
- Tracks: exactly one native LEV and one VRM beside track.ini.
- 50 MiB compressed limit; no ISO, game data, scripts, executables or saves.
- ZIP building and download are local. Authentication is required only
  for upload, and uploads remain private pending moderator approval.

To test the browser ZIP generator with Node.js 24+:
node docs/forge-hub/tests/test_packager_node.cjs
