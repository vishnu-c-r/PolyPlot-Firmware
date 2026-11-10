"use client";

import { useState } from "react";

import type { Illustration, IllustrationCategory } from "./library";
import useCanvas from "../workspace/canvas/CanvasContext";
import { addSvgToPage } from "../workspace/canvas/insertSvg";
import { useEditor } from "../workspace/editor/EditorContext";
import { optimizeSvgPathLifts } from "../export/pathOptimizer";

export const ILLUSTRATION_CATEGORIES: { id: IllustrationCategory; label: string }[] = [
    { id: "single_line", label: "Single-Line Art (Minimal Lifts)" },
    { id: "mandalas", label: "Mandalas & Sacred Geometry" },
    { id: "characters", label: "Characters" },
    { id: "decorative", label: "Decorative & Frames" },
];

export const illustrationUrl = (id: string) => `/api/illustrations/${encodeURIComponent(id)}`;

export type LibraryState =
    | { status: "idle" | "loading" | "error" }
    | { status: "ready"; illustrations: Illustration[] };

/** The illustration catalog from /api/illustrations, loaded on demand. */
export function useIllustrations() {
    const [library, setLibrary] = useState<LibraryState>({ status: "idle" });

    const load = async () => {
        setLibrary({ status: "loading" });
        try {
            const response = await fetch("/api/illustrations");
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const { illustrations } = (await response.json()) as { illustrations: Illustration[] };
            setLibrary({ status: "ready", illustrations });
        } catch (error) {
            console.error("Unable to load illustrations:", error);
            setLibrary({ status: "error" });
        }
    };

    /** Loads the catalog unless it is already loaded or loading. */
    const ensureLoaded = () => {
        if (library.status === "idle" || library.status === "error") void load();
    };

    return { library, load, ensureLoaded };
}

/** Fetches an illustration's SVG and places it in the middle of the page. */
export function useInsertIllustration() {
    const canvasContext = useCanvas();
    const { setTool } = useEditor();
    const [inserting, setInserting] = useState<string | null>(null);
    const [error, setError] = useState<string | null>(null);

    /** Resolves true once the illustration is on the page. */
    const insert = async (illustration: Illustration) => {
        const { canvas, workspace } = canvasContext ?? {};
        if (!canvas || !workspace || inserting) return false;

        setInserting(illustration.id);
        setError(null);
        setTool("Select");
        try {
            const response = await fetch(illustrationUrl(illustration.id));
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            await addSvgToPage(canvas, workspace, await response.text());
            return true;
        } catch (cause) {
            console.error(`Unable to insert ${illustration.name}:`, cause);
            setError(`Couldn't add ${illustration.name}. Try again.`);
            return false;
        } finally {
            setInserting(null);
        }
    };

    return { inserting, error, insert, clearError: () => setError(null) };
}

/** Imports user's custom SVG artwork, optimizes it for minimal pen lifts, and places it on canvas. */
export function useImportCustomArtwork() {
    const canvasContext = useCanvas();
    const { setTool } = useEditor();
    const [importing, setImporting] = useState(false);
    const [error, setError] = useState<string | null>(null);

    const importSvg = async (svgText: string, forceSingleLine = false) => {
        const { canvas, workspace } = canvasContext ?? {};
        if (!canvas || !workspace || importing) return false;

        setImporting(true);
        setError(null);
        setTool("Select");
        try {
            // Run through optimizer to eliminate pen lifts and order paths via nearest-neighbor TSP
            const optimized = optimizeSvgPathLifts(svgText, {
                joinTolerance: 2.0,
                forceSingleLine,
            });
            await addSvgToPage(canvas, workspace, optimized.svg);
            return true;
        } catch (cause) {
            console.error("Unable to import SVG artwork:", cause);
            setError("Couldn't import this SVG artwork. Please verify format.");
            return false;
        } finally {
            setImporting(false);
        }
    };

    return { importSvg, importing, error, clearError: () => setError(null) };
}
