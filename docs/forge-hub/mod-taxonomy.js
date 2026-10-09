/* Shared Forge Hub metadata. Three plain-language kart types are mapped to
   the existing two database fields to preserve all older mod records.
   These labels do not alter game physics or fit wheel models automatically. */
"use strict";
(function () {
    const categories = Object.freeze(["Character", "Kart", "Wheels", "Track"]);
    const maps = Object.freeze(["Race Track", "Battle Arena", "Hub"]);
    const classes = Object.freeze(["Balanced", "Speed", "Acceleration", "Turning"]);
    const drives = Object.freeze(["Wheeled", "Hovercraft"]); // Existing database values
    const wheels = Object.freeze(["Included", "Modular"]);   // Existing database values
    const kartTypes = Object.freeze([
        Object.freeze({
            id: "modular_body",
            label: "Modular Body (Adaptable Wheels)",
            help: "Kart body only. Choose a separate compatible wheel set.",
            kart_drive: "Wheeled", wheel_setup: "Modular"
        }),
        Object.freeze({
            id: "wheeled_kart",
            label: "Modular Kart / Wheeled (Wheels Included)",
            help: "Complete wheeled kart with its own wheels; no separate wheel pack needed.",
            kart_drive: "Wheeled", wheel_setup: "Included"
        }),
        Object.freeze({
            id: "hovercraft",
            label: "Hovercraft (Floating / No Wheels)",
            help: "Floating kart. Wheel packs do not apply.",
            kart_drive: "Hovercraft", wheel_setup: null
        })
    ]);
    const detailKeys = Object.freeze([
        "map_kind", "racer_class", "kart_drive", "wheel_setup"
    ]);

    function fromKartType(id) {
        const found = kartTypes.find(item => item.id === id);
        if (!found) throw new Error("Choose Modular Body, Wheeled Kart or Hovercraft.");
        return {kart_drive: found.kart_drive, wheel_setup: found.wheel_setup};
    }
    function toKartType(value) {
        if (!value || typeof value !== "object") return null;
        const found = kartTypes.find(item =>
            item.kart_drive === value.kart_drive &&
            item.wheel_setup === (value.wheel_setup || null));
        return found ? found.id : null;
    }
    function normalize(category, value, strict = true) {
        if (!categories.includes(category)) throw new Error("Unknown mod category.");
        const details = {};
        for (const key of detailKeys) details[key] = null;
        value = value && typeof value === "object" ? value : {};
        if (category === "Track") {
            details.map_kind = String(value.map_kind || "");
            if (strict && !maps.includes(details.map_kind))
                throw new Error("Select Race Track, Battle Arena or Hub.");
            if (!maps.includes(details.map_kind)) details.map_kind = null;
        }
        if (category === "Character") {
            details.racer_class = String(value.racer_class || "");
            if (strict && !classes.includes(details.racer_class))
                throw new Error("Select a character stats class.");
            if (!classes.includes(details.racer_class)) details.racer_class = null;
        }
        if (category === "Kart") {
            details.kart_drive = String(value.kart_drive || "");
            if (strict && !drives.includes(details.kart_drive))
                throw new Error("Choose Modular Body, Wheeled Kart or Hovercraft.");
            if (!drives.includes(details.kart_drive)) details.kart_drive = null;
            if (details.kart_drive === "Wheeled") {
                details.wheel_setup = String(value.wheel_setup || "");
                if (strict && !wheels.includes(details.wheel_setup))
                    throw new Error("Select Modular Body or Wheeled Kart.");
                if (!wheels.includes(details.wheel_setup)) details.wheel_setup = null;
            } // Hovercraft deliberately has NO wheel_setup
        }
        return details;
    }
    function summary(category, value) {
        const details = normalize(category, value, false);
        if (category === "Track") return details.map_kind || "Map type not specified";
        if (category === "Kart") {
            const kind = toKartType(details);
            return kind === "modular_body" ? "Modular Body / Adaptable Wheels" :
                   kind === "wheeled_kart" ? "Modular Kart / Wheeled (Wheels Included)" :
                   kind === "hovercraft" ? "Hovercraft / No Wheels" :
                   "Kart type not specified";
        }
        if (category === "Character") {
            return details.racer_class ? details.racer_class + " Stats" :
                   "Stats class not specified";
        }
        return "Wheel Set / Separate Add-on";
    }
    window.ForgeModMeta = Object.freeze({
        categories, maps, classes, drives, wheels, kartTypes, detailKeys,
        fromKartType, toKartType, normalize, summary
    });
})();
