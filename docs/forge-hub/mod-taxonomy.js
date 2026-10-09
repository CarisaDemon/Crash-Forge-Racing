/* Shared public metadata vocabulary. Informational labels only:
   selecting a class, track type or hovercraft does not alter game physics. */
"use strict";
(function () {
    const categories = Object.freeze(["Character", "Kart", "Wheels", "Track"]);
    const maps = Object.freeze(["Race Track", "Battle Arena", "Hub"]);
    const classes = Object.freeze(["Balanced", "Speed", "Acceleration", "Turning"]);
    const drives = Object.freeze(["Wheeled", "Hovercraft"]);
    const wheels = Object.freeze(["Included", "Modular"]);

    const detailKeys = Object.freeze([
        "map_kind", "racer_class", "kart_drive", "wheel_setup",
    ]);

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
                throw new Error("Choose Wheeled or Hovercraft.");
            if (!drives.includes(details.kart_drive)) details.kart_drive = null;
            if (details.kart_drive === "Wheeled") {
                details.wheel_setup = String(value.wheel_setup || "");
                if (strict && !wheels.includes(details.wheel_setup))
                    throw new Error("Choose wheels Included or Modular.");
                if (!wheels.includes(details.wheel_setup)) details.wheel_setup = null;
            }
        }
        return details;
    }
    function summary(category, details) {
        details = normalize(category, details, false);
        if (category === "Track") return details.map_kind || "Map type not specified";
        if (category === "Kart") {
            if (details.kart_drive === "Hovercraft") return "Hovercraft / No Wheels";
            if (details.kart_drive === "Wheeled")
                return "Wheeled / " + (details.wheel_setup === "Included" ? "Built-in Wheels" :
                       details.wheel_setup === "Modular" ? "Modular Wheels" : "Wheels unspecified");
            return "Kart drive not specified";
        }
        if (category === "Character") {
            if (!details.racer_class) return "Stats class not specified";
            return details.racer_class + " Stats";
        }
        return "Wheel Set";
    }
    window.ForgeModMeta = Object.freeze({
        categories, maps, classes, drives, wheels, detailKeys, normalize, summary
    });
})();
