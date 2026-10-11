/* Native Crash Forge mod package metadata, distinct from legacy folder categories.
 *
 * New layout:
 *   mods/<readable_mod_name>/<character|kart|wheels|music>.ini
 * with [forge_mod] id=<permanent_web_id>, type=<category>, enabled=<bool>.
 * The folder name is intentionally independent from the web ID.
 *
 * The game does not need a launcher-side registry. This metadata determines
 * discovery; each native [character]/[kart]/... enabled key determines loading.
 */
#ifndef CTR_NATIVE_MOD_PACKAGES_H
#define CTR_NATIVE_MOD_PACKAGES_H

int NativeModPackage_IsValid(const char *slug, const char *kind);

#endif
