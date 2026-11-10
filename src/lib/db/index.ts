import Database from "better-sqlite3";
import { drizzle } from "drizzle-orm/better-sqlite3";
import fs from "node:fs";
import path from "node:path";
import * as schema from "./schema";

const dbFile = process.env.DATABASE_PATH ?? "plotter.db";
const databasePath = path.isAbsolute(dbFile)
    ? dbFile
    : path.join(process.cwd(), "data", path.basename(dbFile));

fs.mkdirSync(path.dirname(databasePath), { recursive: true });

const sqlite = new Database(databasePath, { timeout: 15000 });

sqlite.pragma("busy_timeout = 15000");
try {
    sqlite.pragma("journal_mode = WAL");
} catch {
    // Already in WAL or concurrent build worker
}
sqlite.pragma("foreign_keys = ON");

export const db = drizzle(sqlite, { schema });
export { sqlite };