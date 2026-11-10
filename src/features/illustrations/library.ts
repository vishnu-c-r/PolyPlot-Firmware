import { readFile } from "node:fs/promises";
import path from "node:path";

/**
 * Server-side illustration library: SVG line art kept in ./library with a catalog.
 * To add an illustration, drop a stroke-only SVG into that folder and list it in
 * catalog.json (only artwork you have the rights to).
 */

export type IllustrationCategory = "single_line" | "mandalas" | "characters" | "decorative";

export interface Illustration {
    id: string;
    name: string;
    category: IllustrationCategory;
}

interface CatalogEntry extends Illustration {
    file: string;
}

const LIBRARY_DIR = path.join(process.cwd(), "src/features/illustrations/library");

async function readCatalog(): Promise<CatalogEntry[]> {
    return JSON.parse(await readFile(path.join(LIBRARY_DIR, "catalog.json"), "utf8"));
}

export async function listIllustrations(): Promise<Illustration[]> {
    return (await readCatalog()).map(({ id, name, category }) => ({ id, name, category }));
}

/** The SVG markup for a catalogued illustration, or null for an unknown id. */
export async function readIllustration(id: string): Promise<string | null> {
    // Only catalogued files are ever read, so the id cannot reach other paths.
    const entry = (await readCatalog()).find((item) => item.id === id);
    if (!entry) return null;
    return readFile(path.join(LIBRARY_DIR, entry.file), "utf8");
}
