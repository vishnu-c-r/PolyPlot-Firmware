import type { ConverterSettings } from "svg-to-gcode";

/**
 * Machine profile for the pen plotter behind the Pi gateway. Distances in mm,
 * feeds in mm/min. Heights and the Z feed ceiling come from the gateway spec's
 * safety rules; the gateway clamps too, but G-code made here is already safe.
 */
export const PLOTTER_PROFILE = {
    /** Pen down (drawing depth). */
    zDraw: -4.5,
    /** Pen up (safe travel height). */
    zTravel: -0.5,
    /** No Z move may be faster than this. */
    zFeedMax: 1000,
    /** Drawing feed. Utilizing physical machine speed (up to 15000 mm/min, targeting 8000-12000 mm/min). */
    feedRate: 10000,
    tolerance: 0.3,
} as const;

function envNumber(name: string): number | undefined {
    const raw = process.env[name];
    if (raw === undefined || raw.trim() === "") return undefined;
    const value = Number(raw);
    return Number.isFinite(value) ? value : undefined;
}

/**
 * svg-to-gcode settings for the profile. Where the drawing lands on the bed can be
 * set with PLOTTER_QUADRANT (1-4), PLOTTER_X_OFFSET and PLOTTER_Y_OFFSET.
 */
export function plotterConverterSettings(): ConverterSettings {
    const { zDraw, zTravel, feedRate, tolerance } = PLOTTER_PROFILE;
    const settings: ConverterSettings = {
        zValue: zDraw,
        // svg-to-gcode lifts to zValue + zOffset when zValue is negative.
        zOffset: Math.abs(zTravel - zDraw),
        feedRate,
        tolerance,
    };

    const quadrant = envNumber("PLOTTER_QUADRANT");
    if (quadrant === 1 || quadrant === 2 || quadrant === 3 || quadrant === 4) settings.quadrant = quadrant;

    const xOffset = envNumber("PLOTTER_X_OFFSET");
    if (xOffset !== undefined) settings.xOffset = xOffset;

    const yOffset = envNumber("PLOTTER_Y_OFFSET");
    if (yOffset !== undefined) settings.yOffset = yOffset;

    return settings;
}

export interface ParkPosition {
    x: number;
    y: number;
}

/**
 * Where the pen goes after every plot, with the pen up: PLOTTER_PARK_X / PLOTTER_PARK_Y
 * in mm (default 0, 0, in the same coordinates as the drawing). PLOTTER_PARK=false turns
 * parking off, leaving the pen over the last stroke.
 */
export function plotterParkPosition(): ParkPosition | null {
    if (/^(false|off|no|0)$/i.test(process.env.PLOTTER_PARK?.trim() ?? "")) return null;
    return { x: envNumber("PLOTTER_PARK_X") ?? 0, y: envNumber("PLOTTER_PARK_Y") ?? 0 };
}

const PROGRAM_END = /^M0*(2|30)\b/i;

/**
 * Ends safe G-code with a park move: pen up to the travel height, then a rapid move to
 * the park position. Goes before a trailing M2/M30, which ends the program.
 */
export function appendPark(gcode: string, park: ParkPosition | null, zTravel: number, zFeedMax: number): string {
    if (!park) return gcode;
    const lines = gcode.split("\n").filter((line) => line.trim());
    let end = lines.length;
    while (end > 0 && PROGRAM_END.test(lines[end - 1])) end--;
    const penUp = `G1 Z${format(zTravel)} F${format(zFeedMax)}`;
    const move = `G0 X${format(park.x)} Y${format(park.y)}`;
    // Skip the pen-up when the program already ends with it.
    lines.splice(end, 0, ...(lines[end - 1] === penUp ? [move] : [penUp, move]));
    return lines.join("\n") + "\n";
}

export interface PenSafetyOptions {
    /** Z values are clamped into [zMin, zMax]. */
    zMin: number;
    zMax: number;
    zFeedMax: number;
    /** Feed restored after a Z move when the program hasn't set one yet. */
    feedRate: number;
}

const WORD = /([A-Z])\s*([-+]?(?:\d+\.?\d*|\.\d+))/gi;

const format = (value: number) => String(Math.round(value * 1000) / 1000);

/**
 * Rewrites G-code so pen moves obey the plotter's safety rules:
 * - every Z move becomes `G1 Z… F<=zFeedMax` (a G0 rapid would ignore any feed limit),
 *   with Z clamped between the travel and drawing heights;
 * - the program's drawing feed is restored on the next G1 after a Z move;
 * - motion words are made explicit where a rewrite would change the modal motion;
 * - M400 (unsupported by FluidNC) becomes `G4 P0.05`;
 * - comments and blank lines are removed.
 */
export function applyPenSafety(gcode: string, options: PenSafetyOptions): string {
    const { zMin, zMax, zFeedMax } = options;
    const out: string[] = [];
    // Motion mode as the original program has it (controllers start in G0).
    let modal: "G0" | "G1" = "G0";
    let programFeed = options.feedRate;
    let restoreFeed = false;

    for (const rawLine of gcode.split(/\r?\n/)) {
        const line = rawLine.replace(/\(.*?\)/g, "").replace(/;.*$/, "").trim();
        if (!line) continue;

        if (/^M0*400\b/i.test(line)) {
            out.push("G4 P0.05");
            continue;
        }

        const words = [...line.matchAll(WORD)].map(([, letter, value]) => [letter.toUpperCase(), Number(value)] as const);
        const motionWord = words.find(([letter, value]) => letter === "G" && (value === 0 || value === 1));
        if (motionWord) modal = motionWord[1] === 0 ? "G0" : "G1";

        const has = (letter: string) => words.some(([l]) => l === letter);
        const get = (letter: string) => words.find(([l]) => l === letter)?.[1];
        const feed = get("F");
        if (feed !== undefined && !has("Z")) programFeed = feed;

        const isMotion = has("X") || has("Y") || has("Z");
        const otherG = words.some(([letter, value]) => letter === "G" && value !== 0 && value !== 1);

        if (isMotion && has("Z") && !otherG) {
            const z = Math.min(zMax, Math.max(zMin, get("Z")!));
            const xy = ["X", "Y"].filter(has).map((letter) => `${letter}${format(get(letter)!)}`);
            const zFeed = Math.min(zFeedMax, feed ?? zFeedMax);
            out.push(["G1", ...xy, `Z${format(z)}`, `F${format(zFeed)}`].join(" "));
            restoreFeed = true;
            continue;
        }

        if (isMotion && !otherG) {
            // Lines relying on the modal motion get it spelled out, since rewritten
            // Z moves switch the controller to G1.
            let next = motionWord ? line : `${modal} ${line}`;
            if (modal === "G1" && restoreFeed) {
                if (feed === undefined) next += ` F${format(programFeed)}`;
                restoreFeed = false;
            }
            out.push(next);
            continue;
        }

        out.push(line);
    }

    return out.join("\n") + "\n";
}
