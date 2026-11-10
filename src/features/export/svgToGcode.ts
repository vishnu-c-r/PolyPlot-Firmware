import { Converter, type ConverterSettings } from "svg-to-gcode";

import { PLOTTER_PROFILE, appendPark, applyPenSafety, plotterConverterSettings, plotterParkPosition } from "../plotter/profile";
import { optimizeSvgPathLifts } from "./pathOptimizer";

export interface GcodeResult {
    gcode: string;
    /** Estimated plotting time in seconds. */
    time: number;
    /** Number of G-code lines (what the gateway streams). */
    lines: number;
}

/** Pen-safety limits for the given converter settings. */
function safetyFor(settings: ConverterSettings) {
    const zDraw = settings.zValue ?? PLOTTER_PROFILE.zDraw;
    const zOffset = settings.zOffset ?? 0;
    // Mirrors svg-to-gcode's own pen-up rule.
    const zTravel = zDraw < 0 ? zDraw + zOffset : zDraw - zOffset;

    return {
        zTravel,
        zMin: Math.min(zDraw, zTravel),
        zMax: Math.max(zDraw, zTravel),
        zFeedMax: PLOTTER_PROFILE.zFeedMax,
        feedRate: settings.feedRate ?? PLOTTER_PROFILE.feedRate,
    };
}

export function countLines(gcode: string) {
    return gcode.split("\n").filter((line) => line.trim()).length;
}

/** Pen safety, then the park move at the end (pen up first). */
function finish(gcode: string, settings: ConverterSettings) {
    const safety = safetyFor(settings);
    return appendPark(applyPenSafety(gcode, safety), plotterParkPosition(), safety.zTravel, safety.zFeedMax);
}

/**
 * Converts an SVG (user units read as 96 dpi px) to plotter-safe G-code using the
 * plotter profile, with optional setting overrides. The program ends with the pen
 * parked (see plotterParkPosition).
 * Automatically runs path optimization to eliminate unnecessary pen lifts.
 */
export async function convertSvgToGcode(svg: string, overrides: ConverterSettings = {}): Promise<GcodeResult> {
    const settings = { ...plotterConverterSettings(), ...overrides };
    // Optimize stroke continuity & reorder paths before converting
    const optimized = optimizeSvgPathLifts(svg, { joinTolerance: 3.0 });
    const result = await new Converter(settings).convert(optimized.svg);
    const gcode = finish(result.gcode, settings);

    return { gcode, time: result.time, lines: countLines(gcode) };
}

/** Makes G-code from elsewhere (an uploaded file) plotter-safe with the profile's limits, and parks at the end. */
export function makeGcodeSafe(gcode: string) {
    const safe = finish(gcode, plotterConverterSettings());
    return { gcode: safe, lines: countLines(safe) };
}
