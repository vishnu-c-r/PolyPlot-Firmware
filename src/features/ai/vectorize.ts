import "server-only";

import jpeg from "jpeg-js";
// @ts-expect-error pngjs does not have bundled TS types in this setup
import { PNG } from "pngjs";

// @ts-expect-error potrace internal module
import Potrace from "potrace/lib/Potrace";
// @ts-expect-error potrace internal module
import Bitmap from "potrace/lib/types/Bitmap";
// @ts-expect-error potrace internal module
import utils from "potrace/lib/utils";

import { optimizeSvgPathLifts } from "../export/pathOptimizer";

export interface TraceOptions {
    threshold?: number;
    optTolerance?: number;
    turdSize?: number;
    turnPolicy?: string;
}

/**
 * Decodes a raster image buffer (JPEG or PNG) into raw RGBA pixels.
 */
function decodeImageBuffer(buffer: Buffer): { width: number; height: number; data: Uint8Array | Buffer } {
    if (buffer.length > 8 && buffer[0] === 0x89 && buffer[1] === 0x50 && buffer[2] === 0x4e && buffer[3] === 0x47) {
        const png = PNG.sync.read(buffer);
        return { width: png.width, height: png.height, data: png.data };
    }
    const decoded = jpeg.decode(buffer, { useTArray: true });
    return { width: decoded.width, height: decoded.height, data: decoded.data };
}

function parseCoords(str: string): number[] {
    const matches = str.match(/[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?/g);
    return matches ? matches.map(Number) : [];
}

/**
 * Estimates polyline stroke length of an SVG path 'd' string.
 */
function estimatePathLength(d: string): number {
    const coords = parseCoords(d);
    let len = 0;
    for (let i = 2; i + 1 < coords.length; i += 2) {
        const dx = coords[i] - coords[i - 2];
        const dy = coords[i + 1] - coords[i - 1];
        const dist = Math.hypot(dx, dy);
        if (Number.isFinite(dist)) len += dist;
    }
    return len;
}

/**
 * Clamps Potrace SVG to the longest dominant continuous subpath(s) so plotting finishes
 * in < 45 seconds before water evaporates on slate.
 * Filters out internal hatching, micro-loops, and noise for clean single-line art.
 */
export function clampSubpathsToWaterBudget(svg: string, maxSubpaths = 16, maxBudgetUnits = 5500): string {
    const dMatch = svg.match(/\bd=["']([^"']+)["']/i);
    if (!dMatch) return svg;

    const fullD = dMatch[1];
    const rawSubpaths = fullD.split(/(?=[Mm])/).map((s) => s.trim()).filter(Boolean);
    if (rawSubpaths.length <= 1) return svg;

    // Filter tiny speckle noise (< 40 length)
    const scored = rawSubpaths
        .map((subpath) => ({ subpath, length: estimatePathLength(subpath) }))
        .filter((item) => item.length > 40)
        .sort((a, b) => b.length - a.length);

    if (scored.length === 0) return svg;

    // Pick dominant continuous subpaths within the water budget (< 30s at high speed)
    // Always preserve at least top major contours so the subject is whole and complete
    const selected: string[] = [];
    let totalLen = 0;

    for (const item of scored) {
        if (selected.length < maxSubpaths && (totalLen + item.length <= maxBudgetUnits || selected.length < 4)) {
            selected.push(item.subpath);
            totalLen += item.length;
        }
    }

    const filteredD = selected.join(" ");
    return svg.replace(/\bd=["'][^"']+["']/i, `d="${filteredD}"`);
}

/**
 * Traces a raster image buffer (PNG/JPEG) into a clean, plotter-safe single-line SVG.
 * Replaces potrace's default filled shapes with stroke-only geometry.
 * Bypasses potrace's internal Jimp dependency to ensure 100% compatibility with Next.js Turbopack.
 */
export async function traceImageToSvg(imageBuffer: Buffer, options: TraceOptions = {}): Promise<string> {
    const decoded = decodeImageBuffer(imageBuffer);
    const bitmap = new Bitmap(decoded.width, decoded.height);
    const pixels = decoded.data;

    for (let i = 0; i < bitmap.size; i++) {
        const idx = i * 4;
        const opacity = pixels[idx + 3] / 255;
        const r = 255 + (pixels[idx] - 255) * opacity;
        const g = 255 + (pixels[idx + 1] - 255) * opacity;
        const b = 255 + (pixels[idx + 2] - 255) * opacity;
        bitmap.data[i] = utils.luminance(r, g, b);
    }

    const potrace = new Potrace({
        threshold: options.threshold ?? 160,
        optTolerance: options.optTolerance ?? 1.2,
        turdSize: options.turdSize ?? 30,
        turnPolicy: options.turnPolicy ?? "minority",
    });

    potrace._luminanceData = bitmap;
    potrace._imageLoaded = true;

    const rawSvg: string = potrace.getSVG();

    // Convert potrace's filled silhouettes into clean stroke geometry for pen plotter
    let strokeSvg = rawSvg.replace('stroke="none" fill="black"', 'stroke="#000" fill="none" stroke-width="1.5"');
    if (!strokeSvg.includes('stroke=')) {
        strokeSvg = strokeSvg.replace('<path ', '<path stroke="#000" fill="none" stroke-width="1.5" ');
    }

    // Filter to dominant continuous contours under 45s water budget (target 15-30s)
    const clampedSvg = clampSubpathsToWaterBudget(strokeSvg, 16, 5500);
    // Chain adjacent endpoints into continuous strokes to minimize pen lifts
    const opt = optimizeSvgPathLifts(clampedSvg, { joinTolerance: 4.0 });
    return opt.svg;
}
