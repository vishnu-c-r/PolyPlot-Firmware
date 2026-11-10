/**
 * SVG Path Optimizer for Pen Plotters
 *
 * Minimizes or eliminates pen lifts (Z-axis movements) by:
 * 1. Extracting all geometric elements (paths, lines, polylines, polygons, circles,
 *    ellipses, rects) while preserving group matrix/rotation/translation transforms.
 * 2. Normalizing all commands (absolute and relative) into standard absolute geometry.
 * 3. Accurately determining exact start and end coordinates of every subpath.
 * 4. Solving a greedy nearest-neighbor TSP to optimize plotting order.
 * 5. Bidirectionally evaluating paths and auto-reversing segments (including exact
 *    mathematical inversion of Cubic/Quadratic Béziers, Lines, and Arcs) when the
 *    tail is closer to the next segment than the head.
 * 6. Chaining adjacent endpoints within `joinTolerance` into continuous drawing paths
 *    with zero pen-up hops.
 * 7. Optionally forcing a 100% continuous single-line art path.
 */

import { XMLParser } from "fast-xml-parser";

export interface OptimizationOptions {
    /** Max distance in mm / SVG units to automatically merge endpoints into 1 continuous stroke (default: 3.0) */
    joinTolerance?: number;
    /** If true, draws a subtle bridge line between all remaining disconnected contours for 100% 1-line art */
    forceSingleLine?: boolean;
}

export interface OptimizationResult {
    svg: string;
    originalPathsCount: number;
    optimizedPathsCount: number;
    penLiftsSaved: number;
}

interface Matrix {
    a: number;
    b: number;
    c: number;
    d: number;
    e: number;
    f: number;
}

const IDENTITY: Matrix = { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 };

function multiplyMatrix(m1: Matrix, m2: Matrix): Matrix {
    return {
        a: m1.a * m2.a + m1.c * m2.b,
        b: m1.b * m2.a + m1.d * m2.b,
        c: m1.a * m2.c + m1.c * m2.d,
        d: m1.b * m2.c + m1.d * m2.d,
        e: m1.a * m2.e + m1.c * m2.f + m1.e,
        f: m1.b * m2.e + m1.d * m2.f + m1.f,
    };
}

function applyMatrixToPoint(m: Matrix, p: [number, number]): [number, number] {
    return [
        m.a * p[0] + m.c * p[1] + m.e,
        m.b * p[0] + m.d * p[1] + m.f,
    ];
}

function parseTransform(val: string): Matrix {
    let current = IDENTITY;
    const re = /(matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]+)\)/gi;
    let match: RegExpExecArray | null;
    while ((match = re.exec(val)) !== null) {
        const type = match[1].toLowerCase();
        const args = match[2].trim().split(/[\s,]+/).map(Number).filter((n) => !isNaN(n));
        let step = IDENTITY;
        if (type === "matrix" && args.length >= 6) {
            step = { a: args[0], b: args[1], c: args[2], d: args[3], e: args[4], f: args[5] };
        } else if (type === "translate") {
            step = { a: 1, b: 0, c: 0, d: 1, e: args[0], f: args[1] ?? 0 };
        } else if (type === "scale") {
            step = { a: args[0], b: 0, c: 0, d: args[1] ?? args[0], e: 0, f: 0 };
        } else if (type === "rotate") {
            const rad = (args[0] * Math.PI) / 180;
            const cos = Math.cos(rad);
            const sin = Math.sin(rad);
            if (args.length >= 3) {
                const cx = args[1];
                const cy = args[2];
                const t1: Matrix = { a: 1, b: 0, c: 0, d: 1, e: cx, f: cy };
                const rot: Matrix = { a: cos, b: sin, c: -sin, d: cos, e: 0, f: 0 };
                const t2: Matrix = { a: 1, b: 0, c: 0, d: 1, e: -cx, f: -cy };
                step = multiplyMatrix(multiplyMatrix(t1, rot), t2);
            } else {
                step = { a: cos, b: sin, c: -sin, d: cos, e: 0, f: 0 };
            }
        }
        current = multiplyMatrix(current, step);
    }
    return current;
}

type SegmentCommand =
    | { type: "M"; x: number; y: number }
    | { type: "L"; x: number; y: number }
    | { type: "C"; x1: number; y1: number; x2: number; y2: number; x: number; y: number }
    | { type: "Q"; x1: number; y1: number; x: number; y: number }
    | { type: "A"; rx: number; ry: number; rot: number; large: number; sweep: number; x: number; y: number }
    | { type: "Z" };

interface Subpath {
    commands: SegmentCommand[];
    start: [number, number];
    end: [number, number];
    isClosed: boolean;
}

function parseCoords(str: string): number[] {
    const matches = str.match(/[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?/g);
    return matches ? matches.map(Number) : [];
}

/**
 * Parses path data string into normalized absolute Subpaths.
 * Converts relative commands (l, c, q, etc.) and H/V lines into standard absolute coordinates.
 */
function parsePathData(d: string): Subpath[] {
    const subpaths: Subpath[] = [];
    let currentCommands: SegmentCommand[] = [];
    let currX = 0;
    let currY = 0;
    let startX = 0;
    let startY = 0;
    let lastCubicCtrl: [number, number] | null = null;
    let lastQuadCtrl: [number, number] | null = null;

    const tokens = d.match(/([a-df-z])|([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)/gi);
    if (!tokens) return [];

    let idx = 0;
    let cmd = "";

    const VALID_COMMANDS = new Set(["M", "L", "H", "V", "C", "S", "Q", "T", "A", "Z"]);

    const nextNum = () => {
        if (idx >= tokens.length) return 0;
        if (VALID_COMMANDS.has(tokens[idx].toUpperCase())) return 0;
        const n = parseFloat(tokens[idx++]);
        return isNaN(n) ? 0 : n;
    };

    const flushCurrent = () => {
        if (currentCommands.length > 0) {
            const hasDrawing = currentCommands.some((c) => c.type !== "M");
            if (hasDrawing) {
                const isClosed = currentCommands[currentCommands.length - 1].type === "Z";
                subpaths.push({
                    commands: currentCommands,
                    start: [startX, startY],
                    end: isClosed ? [startX, startY] : [currX, currY],
                    isClosed,
                });
            }
            currentCommands = [];
        }
    };

    while (idx < tokens.length) {
        const token = tokens[idx++];
        const upperToken = token.toUpperCase();
        if (VALID_COMMANDS.has(upperToken)) {
            cmd = token;
        } else if (cmd && VALID_COMMANDS.has(cmd.toUpperCase())) {
            idx--;
            if (cmd === "M") cmd = "L";
            else if (cmd === "m") cmd = "l";
        } else {
            // Stray number or unrecognized token without a valid active command: skip safely
            cmd = "";
            continue;
        }

        const isRel = cmd === cmd.toLowerCase();
        const upper = cmd.toUpperCase();

        if (upper === "M") {
            const x = nextNum() + (isRel ? currX : 0);
            const y = nextNum() + (isRel ? currY : 0);
            flushCurrent();
            currX = startX = x;
            currY = startY = y;
            currentCommands.push({ type: "M", x, y });
            lastCubicCtrl = lastQuadCtrl = null;
        } else if (upper === "L") {
            const x = nextNum() + (isRel ? currX : 0);
            const y = nextNum() + (isRel ? currY : 0);
            currentCommands.push({ type: "L", x, y });
            currX = x;
            currY = y;
            lastCubicCtrl = lastQuadCtrl = null;
        } else if (upper === "H") {
            const x = nextNum() + (isRel ? currX : 0);
            currentCommands.push({ type: "L", x, y: currY });
            currX = x;
            lastCubicCtrl = lastQuadCtrl = null;
        } else if (upper === "V") {
            const y = nextNum() + (isRel ? currY : 0);
            currentCommands.push({ type: "L", x: currX, y });
            currY = y;
            lastCubicCtrl = lastQuadCtrl = null;
        } else if (upper === "C") {
            const x1 = nextNum() + (isRel ? currX : 0);
            const y1 = nextNum() + (isRel ? currY : 0);
            const x2 = nextNum() + (isRel ? currX : 0);
            const y2 = nextNum() + (isRel ? currY : 0);
            const x = nextNum() + (isRel ? currX : 0);
            const y = nextNum() + (isRel ? currY : 0);
            currentCommands.push({ type: "C", x1, y1, x2, y2, x, y });
            lastCubicCtrl = [x2, y2];
            currX = x;
            currY = y;
            lastQuadCtrl = null;
        } else if (upper === "S") {
            const x1: number = lastCubicCtrl ? 2 * currX - lastCubicCtrl[0] : currX;
            const y1: number = lastCubicCtrl ? 2 * currY - lastCubicCtrl[1] : currY;
            const x2 = nextNum() + (isRel ? currX : 0);
            const y2 = nextNum() + (isRel ? currY : 0);
            const x = nextNum() + (isRel ? currX : 0);
            const y = nextNum() + (isRel ? currY : 0);
            currentCommands.push({ type: "C", x1, y1, x2, y2, x, y });
            lastCubicCtrl = [x2, y2];
            currX = x;
            currY = y;
            lastQuadCtrl = null;
        } else if (upper === "Q") {
            const x1 = nextNum() + (isRel ? currX : 0);
            const y1 = nextNum() + (isRel ? currY : 0);
            const x = nextNum() + (isRel ? currX : 0);
            const y = nextNum() + (isRel ? currY : 0);
            currentCommands.push({ type: "Q", x1, y1, x, y });
            lastQuadCtrl = [x1, y1];
            currX = x;
            currY = y;
            lastCubicCtrl = null;
        } else if (upper === "T") {
            const x1: number = lastQuadCtrl ? 2 * currX - lastQuadCtrl[0] : currX;
            const y1: number = lastQuadCtrl ? 2 * currY - lastQuadCtrl[1] : currY;
            const x = nextNum() + (isRel ? currX : 0);
            const y = nextNum() + (isRel ? currY : 0);
            currentCommands.push({ type: "Q", x1, y1, x, y });
            lastQuadCtrl = [x1, y1];
            currX = x;
            currY = y;
            lastCubicCtrl = null;
        } else if (upper === "A") {
            const rx = Math.abs(nextNum());
            const ry = Math.abs(nextNum());
            const rot = nextNum();
            const large = nextNum();
            const sweep = nextNum();
            const x = nextNum() + (isRel ? currX : 0);
            const y = nextNum() + (isRel ? currY : 0);
            currentCommands.push({ type: "A", rx, ry, rot, large, sweep, x, y });
            currX = x;
            currY = y;
            lastCubicCtrl = lastQuadCtrl = null;
        } else if (upper === "Z") {
            currentCommands.push({ type: "Z" });
            currX = startX;
            currY = startY;
            lastCubicCtrl = lastQuadCtrl = null;
        }
    }

    flushCurrent();

    return subpaths;
}

/**
 * Transforms all coordinates in a subpath by an affine 2D matrix.
 */
function transformSubpath(subpath: Subpath, matrix: Matrix): Subpath {
    if (matrix.a === 1 && matrix.b === 0 && matrix.c === 0 && matrix.d === 1 && matrix.e === 0 && matrix.f === 0) {
        return subpath;
    }

    const scaleX = Math.hypot(matrix.a, matrix.b);
    const scaleY = Math.hypot(matrix.c, matrix.d);
    const det = matrix.a * matrix.d - matrix.b * matrix.c;

    const transformedCmds: SegmentCommand[] = subpath.commands.map((cmd) => {
        switch (cmd.type) {
            case "M": {
                const [x, y] = applyMatrixToPoint(matrix, [cmd.x, cmd.y]);
                return { type: "M", x, y };
            }
            case "L": {
                const [x, y] = applyMatrixToPoint(matrix, [cmd.x, cmd.y]);
                return { type: "L", x, y };
            }
            case "C": {
                const [x1, y1] = applyMatrixToPoint(matrix, [cmd.x1, cmd.y1]);
                const [x2, y2] = applyMatrixToPoint(matrix, [cmd.x2, cmd.y2]);
                const [x, y] = applyMatrixToPoint(matrix, [cmd.x, cmd.y]);
                return { type: "C", x1, y1, x2, y2, x, y };
            }
            case "Q": {
                const [x1, y1] = applyMatrixToPoint(matrix, [cmd.x1, cmd.y1]);
                const [x, y] = applyMatrixToPoint(matrix, [cmd.x, cmd.y]);
                return { type: "Q", x1, y1, x, y };
            }
            case "A": {
                const [x, y] = applyMatrixToPoint(matrix, [cmd.x, cmd.y]);
                // If reflection (negative determinant), invert sweep flag to preserve correct curvature direction
                const sweep = det < 0 ? (cmd.sweep === 0 ? 1 : 0) : cmd.sweep;
                return {
                    type: "A",
                    rx: cmd.rx * scaleX,
                    ry: cmd.ry * scaleY,
                    rot: cmd.rot,
                    large: cmd.large,
                    sweep,
                    x,
                    y,
                };
            }
            case "Z":
                return { type: "Z" };
        }
    });

    const start = applyMatrixToPoint(matrix, subpath.start);
    const end = subpath.isClosed ? start : applyMatrixToPoint(matrix, subpath.end);

    return {
        commands: transformedCmds,
        start,
        end,
        isClosed: subpath.isClosed,
    };
}

/**
 * Reverses a subpath with exact mathematical inversion of curves and lines.
 * Returns null if subpath is closed or contains non-invertible commands.
 */
function reverseSubpath(subpath: Subpath): Subpath | null {
    if (subpath.isClosed) return null;
    const cmds = subpath.commands;
    if (cmds.length < 2) return null;

    // Track intermediate points from forward pass
    const points: [number, number][] = [subpath.start];
    for (let i = 1; i < cmds.length; i++) {
        const c = cmds[i];
        if (c.type === "L" || c.type === "C" || c.type === "Q" || c.type === "A") {
            points.push([c.x, c.y]);
        }
    }

    if (points.length !== cmds.length) return null;

    const reversedCmds: SegmentCommand[] = [{ type: "M", x: subpath.end[0], y: subpath.end[1] }];

    for (let i = cmds.length - 1; i >= 1; i--) {
        const c = cmds[i];
        const prevPt = points[i - 1];
        if (!prevPt) return null;

        if (c.type === "L") {
            reversedCmds.push({ type: "L", x: prevPt[0], y: prevPt[1] });
        } else if (c.type === "C") {
            reversedCmds.push({
                type: "C",
                x1: c.x2,
                y1: c.y2,
                x2: c.x1,
                y2: c.y1,
                x: prevPt[0],
                y: prevPt[1],
            });
        } else if (c.type === "Q") {
            reversedCmds.push({
                type: "Q",
                x1: c.x1,
                y1: c.y1,
                x: prevPt[0],
                y: prevPt[1],
            });
        } else if (c.type === "A") {
            reversedCmds.push({
                type: "A",
                rx: c.rx,
                ry: c.ry,
                rot: c.rot,
                large: c.large,
                sweep: c.sweep === 0 ? 1 : 0,
                x: prevPt[0],
                y: prevPt[1],
            });
        } else {
            return null;
        }
    }

    return {
        commands: reversedCmds,
        start: [subpath.end[0], subpath.end[1]],
        end: [subpath.start[0], subpath.start[1]],
        isClosed: false,
    };
}

function dist(p1: [number, number], p2: [number, number]): number {
    const dx = p1[0] - p2[0];
    const dy = p1[1] - p2[1];
    return Math.sqrt(dx * dx + dy * dy);
}

function concatenateSubpaths(first: Subpath, second: Subpath): Subpath {
    const cmds = [...first.commands];
    if (cmds[cmds.length - 1].type === "Z") {
        cmds.pop();
        cmds.push({ type: "L", x: first.end[0], y: first.end[1] });
    }

    const gap = dist(first.end, second.start);
    if (gap > 0.05) {
        cmds.push({ type: "L", x: second.start[0], y: second.start[1] });
    }

    const tailCmds = second.commands.slice(1);
    if (tailCmds.length > 0 && tailCmds[tailCmds.length - 1].type === "Z") {
        tailCmds.pop();
        tailCmds.push({ type: "L", x: second.start[0], y: second.start[1] });
    }
    cmds.push(...tailCmds);

    return {
        commands: cmds,
        start: first.start,
        end: second.end,
        isClosed: false,
    };
}

/**
 * Parses all geometric elements and subpaths from an SVG string, preserving
 * group matrices, rotations, and shapes (paths, lines, polylines, polygons, circles, rects, ellipses).
 */
function extractAllSubpaths(svgText: string): { subpaths: Subpath[]; viewBox: string } {
    const parser = new XMLParser({
        preserveOrder: true,
        ignoreAttributes: false,
        attributeNamePrefix: "@_",
        trimValues: true,
    });

    let viewBox = "0 0 200 200";
    const subpaths: Subpath[] = [];

    let parsedTree: any[];
    try {
        parsedTree = parser.parse(svgText);
    } catch {
        return { subpaths: [], viewBox };
    }

    function walkNodes(nodes: any[], currentMatrix: Matrix) {
        for (const node of nodes) {
            const keys = Object.keys(node).filter((k) => k !== ":@" && !k.startsWith("#"));
            for (const tag of keys) {
                const attrs = node[":@"] ?? {};
                let localMatrix = currentMatrix;
                if (attrs["@_transform"]) {
                    localMatrix = multiplyMatrix(currentMatrix, parseTransform(attrs["@_transform"]));
                }

                if (tag === "svg") {
                    if (attrs["@_viewBox"]) {
                        viewBox = attrs["@_viewBox"];
                    } else if (attrs["@_width"] && attrs["@_height"]) {
                        const w = parseFloat(attrs["@_width"]);
                        const h = parseFloat(attrs["@_height"]);
                        if (w > 0 && h > 0) viewBox = `0 0 ${w} ${h}`;
                    }
                    if (Array.isArray(node[tag])) walkNodes(node[tag], localMatrix);
                } else if (tag === "g") {
                    if (Array.isArray(node[tag])) walkNodes(node[tag], localMatrix);
                } else if (tag === "path") {
                    const d = attrs["@_d"];
                    if (d) {
                        const parsed = parsePathData(d);
                        for (const s of parsed) subpaths.push(transformSubpath(s, localMatrix));
                    }
                } else if (tag === "line") {
                    const x1 = parseFloat(attrs["@_x1"] ?? "0");
                    const y1 = parseFloat(attrs["@_y1"] ?? "0");
                    const x2 = parseFloat(attrs["@_x2"] ?? "0");
                    const y2 = parseFloat(attrs["@_y2"] ?? "0");
                    const d = `M ${x1} ${y1} L ${x2} ${y2}`;
                    const parsed = parsePathData(d);
                    for (const s of parsed) subpaths.push(transformSubpath(s, localMatrix));
                } else if (tag === "polyline" || tag === "polygon") {
                    const pts = parseCoords(attrs["@_points"] ?? "");
                    if (pts.length >= 4) {
                        let d = `M ${pts[0]} ${pts[1]}`;
                        for (let i = 2; i + 1 < pts.length; i += 2) d += ` L ${pts[i]} ${pts[i + 1]}`;
                        if (tag === "polygon") d += " Z";
                        const parsed = parsePathData(d);
                        for (const s of parsed) subpaths.push(transformSubpath(s, localMatrix));
                    }
                } else if (tag === "circle") {
                    const cx = parseFloat(attrs["@_cx"] ?? "0");
                    const cy = parseFloat(attrs["@_cy"] ?? "0");
                    const r = parseFloat(attrs["@_r"] ?? "0");
                    if (r > 0) {
                        const d = `M ${cx - r} ${cy} A ${r} ${r} 0 1 0 ${cx + r} ${cy} A ${r} ${r} 0 1 0 ${cx - r} ${cy} Z`;
                        const parsed = parsePathData(d);
                        for (const s of parsed) subpaths.push(transformSubpath(s, localMatrix));
                    }
                } else if (tag === "ellipse") {
                    const cx = parseFloat(attrs["@_cx"] ?? "0");
                    const cy = parseFloat(attrs["@_cy"] ?? "0");
                    const rx = parseFloat(attrs["@_rx"] ?? "0");
                    const ry = parseFloat(attrs["@_ry"] ?? "0");
                    if (rx > 0 && ry > 0) {
                        const d = `M ${cx - rx} ${cy} A ${rx} ${ry} 0 1 0 ${cx + rx} ${cy} A ${rx} ${ry} 0 1 0 ${cx - rx} ${cy} Z`;
                        const parsed = parsePathData(d);
                        for (const s of parsed) subpaths.push(transformSubpath(s, localMatrix));
                    }
                } else if (tag === "rect") {
                    const x = parseFloat(attrs["@_x"] ?? "0");
                    const y = parseFloat(attrs["@_y"] ?? "0");
                    const w = parseFloat(attrs["@_width"] ?? "0");
                    const h = parseFloat(attrs["@_height"] ?? "0");
                    const rx = parseFloat(attrs["@_rx"] ?? "0");
                    const ry = parseFloat(attrs["@_ry"] ?? "0");
                    if (w > 0 && h > 0) {
                        let d: string;
                        if (rx > 0 || ry > 0) {
                            const r_x = Math.min(rx > 0 ? rx : ry, w / 2);
                            const r_y = Math.min(ry > 0 ? ry : rx, h / 2);
                            d = `M ${x + r_x} ${y} H ${x + w - r_x} A ${r_x} ${r_y} 0 0 1 ${x + w} ${y + r_y} V ${y + h - r_y} A ${r_x} ${r_y} 0 0 1 ${x + w - r_x} ${y + h} H ${x + r_x} A ${r_x} ${r_y} 0 0 1 ${x} ${y + h - r_y} V ${y + r_y} A ${r_x} ${r_y} 0 0 1 ${x + r_x} ${y} Z`;
                        } else {
                            d = `M ${x} ${y} H ${x + w} V ${y + h} H ${x} Z`;
                        }
                        const parsed = parsePathData(d);
                        for (const s of parsed) subpaths.push(transformSubpath(s, localMatrix));
                    }
                }
            }
        }
    }

    walkNodes(parsedTree, IDENTITY);
    return { subpaths, viewBox };
}

function serializeCmds(cmds: SegmentCommand[]): string {
    const fmt = (n: number) => String(Math.round(n * 100) / 100);
    return cmds.map((c) => {
        switch (c.type) {
            case "M": return `M ${fmt(c.x)} ${fmt(c.y)}`;
            case "L": return `L ${fmt(c.x)} ${fmt(c.y)}`;
            case "C": return `C ${fmt(c.x1)} ${fmt(c.y1)} ${fmt(c.x2)} ${fmt(c.y2)} ${fmt(c.x)} ${fmt(c.y)}`;
            case "Q": return `Q ${fmt(c.x1)} ${fmt(c.y1)} ${fmt(c.x)} ${fmt(c.y)}`;
            case "A": return `A ${fmt(c.rx)} ${fmt(c.ry)} ${c.rot} ${c.large} ${c.sweep} ${fmt(c.x)} ${fmt(c.y)}`;
            case "Z": return "Z";
        }
    }).join(" ");
}

/**
 * Optimizes an SVG's path order and joins proximate endpoints to minimize pen lifts.
 */
export function optimizeSvgPathLifts(
    svgString: string,
    options: OptimizationOptions = {}
): OptimizationResult {
    const joinTolerance = options.joinTolerance ?? 3.0;
    const forceSingleLine = options.forceSingleLine ?? false;

    const { subpaths, viewBox } = extractAllSubpaths(svgString);
    if (!subpaths.length) {
        return {
            svg: svgString,
            originalPathsCount: 0,
            optimizedPathsCount: 0,
            penLiftsSaved: 0,
        };
    }

    const originalCount = subpaths.length;
    const pool: Subpath[] = [...subpaths];

    // Phase 1: Agglomerative stitch-merging of adjacent endpoints within joinTolerance
    let merged = true;
    while (merged && pool.length > 1) {
        merged = false;
        let bestDist = joinTolerance + 1e-9;
        let bestI = -1;
        let bestJ = -1;
        let bestType: "AB" | "ABrev" | "BA" | "ArevB" = "AB";

        for (let i = 0; i < pool.length; i++) {
            const a = pool[i];
            for (let j = 0; j < pool.length; j++) {
                if (i === j) continue;
                const b = pool[j];

                // Case 1: a.end -> b.start
                const d1 = dist(a.end, b.start);
                if (d1 < bestDist) {
                    bestDist = d1;
                    bestI = i;
                    bestJ = j;
                    bestType = "AB";
                }

                // Case 2: a.end -> b.end (reverse b)
                if (!b.isClosed) {
                    const d2 = dist(a.end, b.end);
                    if (d2 < bestDist) {
                        bestDist = d2;
                        bestI = i;
                        bestJ = j;
                        bestType = "ABrev";
                    }
                }

                // Case 3: b.end -> a.start
                if (!a.isClosed) {
                    const d3 = dist(b.end, a.start);
                    if (d3 < bestDist) {
                        bestDist = d3;
                        bestI = i;
                        bestJ = j;
                        bestType = "BA";
                    }
                }

                // Case 4: a.start -> b.start (reverse a)
                if (!a.isClosed && !b.isClosed) {
                    const d4 = dist(a.start, b.start);
                    if (d4 < bestDist) {
                        bestDist = d4;
                        bestI = i;
                        bestJ = j;
                        bestType = "ArevB";
                    }
                }
            }
        }

        if (bestI >= 0 && bestJ >= 0 && bestDist <= joinTolerance) {
            const a = pool[bestI];
            const b = pool[bestJ];
            let combined: Subpath | null = null;

            if (bestType === "AB") {
                combined = concatenateSubpaths(a, b);
            } else if (bestType === "ABrev") {
                const bRev = reverseSubpath(b);
                if (bRev) combined = concatenateSubpaths(a, bRev);
            } else if (bestType === "BA") {
                combined = concatenateSubpaths(b, a);
            } else if (bestType === "ArevB") {
                const aRev = reverseSubpath(a);
                if (aRev) combined = concatenateSubpaths(aRev, b);
            }

            if (combined) {
                const idx1 = Math.max(bestI, bestJ);
                const idx2 = Math.min(bestI, bestJ);
                pool.splice(idx1, 1);
                pool.splice(idx2, 1);
                pool.push(combined);
                merged = true;
            }
        }
    }

    // Phase 2: TSP ordering of remaining disconnected components
    const unvisited = new Set(pool);
    const ordered: Subpath[] = [];
    const current = pool[0];
    unvisited.delete(current);
    ordered.push(current);

    while (unvisited.size > 0) {
        const lastEnd = ordered[ordered.length - 1].end;
        let bestSub: Subpath | null = null;
        let bestDist = Infinity;
        let chosenReversed: Subpath | null = null;

        for (const cand of unvisited) {
            const dForward = dist(lastEnd, cand.start);
            if (dForward < bestDist) {
                bestDist = dForward;
                bestSub = cand;
                chosenReversed = null;
            }

            if (!cand.isClosed) {
                const dRev = dist(lastEnd, cand.end);
                if (dRev < bestDist) {
                    const rev = reverseSubpath(cand);
                    if (rev) {
                        bestDist = dRev;
                        bestSub = cand;
                        chosenReversed = rev;
                    }
                }
            }
        }

        if (bestSub) {
            unvisited.delete(bestSub);
            ordered.push(chosenReversed ?? bestSub);
        } else {
            break;
        }
    }

    // Phase 3: Bridge remaining paths if forceSingleLine is requested
    let finalPaths: string[] = [];
    if (forceSingleLine && ordered.length > 1) {
        let single = ordered[0];
        for (let i = 1; i < ordered.length; i++) {
            single = concatenateSubpaths(single, ordered[i]);
        }
        finalPaths = [serializeCmds(single.commands)];
    } else {
        finalPaths = ordered.map((s) => serializeCmds(s.commands));
    }

    const savedLifts = Math.max(0, originalCount - finalPaths.length);

    let newSvg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}" fill="none" stroke="#000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">\n`;
    for (const d of finalPaths) {
        newSvg += `  <path d="${d.trim()}" />\n`;
    }
    newSvg += `</svg>`;

    return {
        svg: newSvg,
        originalPathsCount: originalCount,
        optimizedPathsCount: finalPaths.length,
        penLiftsSaved: savedLifts,
    };
}
