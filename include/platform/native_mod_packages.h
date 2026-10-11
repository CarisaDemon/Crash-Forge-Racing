/* Native Crash Forge mod package metadata, distinct from legacy folder categories.
 *
 * New layout:
 *   assets/mods/<permanent_web_id>/<character|kart|wheels|music>.ini
 * with a [forge_mod] section: id=<folder>, type=<category>, enabled=<bool>.
 *
 * The game does not need a launcher-side registry. This metadata determines
 * discovery; each native [character]/[kart]/... enabled key determines loading.
 */
#ifndef CTR_NATIVE_MOD_PACKAGES_H
#define CTR_NATIVE_MOD_PACKAGES_H

int NativeModPackage_IsValid(const char *slug, const char *kind);

#endif
