import "server-only";

import { z } from "zod";

/** AI configuration error: the feature is unavailable until the environment is fixed. */
export class AiConfigError extends Error {
    constructor(message: string) {
        super(message);
        this.name = "AiConfigError";
    }
}

export const AI_PROVIDERS = ["gemini", "groq"] as const;
export type AiProvider = (typeof AI_PROVIDERS)[number];

/**
 * Free-tier Flash models, tried in order. Each has its own Free Tier daily limit (about
 * 20 requests), and a busy one answers 503, so the next is used when one can't answer.
 * Lite models come last: weaker drawings, but usually available. Not listed: Pro and
 * Argon (no free-tier quota), gemini-3.5-flash (it shares gemini-3.6-flash's quota)
 * and preview models (the SDK uses the stable v1 API, which doesn't serve them).
 */
export const DEFAULT_GEMINI_MODELS = ["gemini-3.8-flash", "gemini-3.7-flash", "gemini-3.6-flash", "gemini-3.5-flash-lite", "gemini-3.1-flash-lite"];

const booleanFlag = z
    .string()
    .trim()
    .toLowerCase()
    .transform((value) => ["1", "true", "yes", "on"].includes(value));

const optionalText = z
    .string()
    .trim()
    .optional()
    .transform((value) => value || undefined);

/** Settings shared by every provider. */
const SharedSchema = z.object({
    AI_PROVIDER: z
        .string()
        .trim()
        .toLowerCase()
        .optional()
        .transform((value) => value || (process.env.GROQ_API_KEY && !process.env.GEMINI_API_KEY ? "groq" : "gemini"))
        .pipe(z.enum(AI_PROVIDERS, { error: "AI_PROVIDER must be gemini or groq" })),
    AI_MAX_OUTPUT_TOKENS: z.coerce.number().int().min(256).max(64_000),
    AI_ENABLE_WEB_SEARCH: booleanFlag.default(false),
    AI_SVG_WIDTH_MM: z.coerce.number().positive().max(5000),
    AI_SVG_HEIGHT_MM: z.coerce.number().positive().max(5000),
});

const GeminiSchema = z.object({
    GEMINI_API_KEY: z.string().trim().min(1, "GEMINI_API_KEY is not set"),
    GEMINI_MODEL: optionalText,
});

const GroqSchema = z.object({
    GROQ_API_KEY: z.string().trim().min(1, "GROQ_API_KEY is not set"),
    GROQ_BASE_URL: z.url("GROQ_BASE_URL must be a URL"),
    GROQ_MODEL: z.string().trim().min(1, "GROQ_MODEL is not set"),
});

export interface AiConfig {
    provider: AiProvider;
    apiKey: string;
    /** First model to try. */
    model: string;
    /** Models to try in order (Gemini fallback chain); `[model]` for Groq. */
    models: string[];
    /** Provider endpoint, when the provider needs one (Groq). */
    baseUrl?: string;
    maxOutputTokens: number;
    webSearchEnabled: boolean;
    /** Plotter working area in mm. */
    area: { width: number; height: number };
}

let warnedAboutGroqKey = false;

function parse<T>(schema: z.ZodType<T>): T {
    const parsed = schema.safeParse(process.env);
    if (!parsed.success) {
        const fields = [...new Set(parsed.error.issues.map((issue) => String(issue.path[0])))].join(", ");
        throw new AiConfigError(`AI settings missing or invalid: ${fields}`);
    }
    return parsed.data;
}

/**
 * Reads and validates the AI settings from the environment (server only). Only the
 * selected provider's settings (AI_PROVIDER, default gemini) are required.
 */
export function getAiConfig(): AiConfig {
    const shared = parse(SharedSchema);
    const common = {
        maxOutputTokens: shared.AI_MAX_OUTPUT_TOKENS,
        webSearchEnabled: shared.AI_ENABLE_WEB_SEARCH,
        area: { width: shared.AI_SVG_WIDTH_MM, height: shared.AI_SVG_HEIGHT_MM },
    };

    if (shared.AI_PROVIDER === "groq") {
        const env = parse(GroqSchema);
        if (!warnedAboutGroqKey && !env.GROQ_API_KEY.startsWith("gsk_") && !env.GROQ_API_KEY.startsWith("AQ.") && !env.GROQ_API_KEY.startsWith("AIza")) {
            warnedAboutGroqKey = true;
            console.warn("[AI] GROQ_API_KEY doesn't look like a standard key (expected gsk_, AQ., or AIza).");
        }
        return {
            provider: "groq",
            apiKey: env.GROQ_API_KEY,
            model: env.GROQ_MODEL,
            models: [env.GROQ_MODEL],
            baseUrl: env.GROQ_BASE_URL.replace(/\/+$/, ""),
            ...common,
        };
    }

    // Gemini keys come in more than one format (AIza…, and newer ones), so no shape check.
    const env = parse(GeminiSchema);
    // GEMINI_MODEL takes one model or a comma-separated fallback list.
    const listed = env.GEMINI_MODEL?.split(",").map((model) => model.trim()).filter(Boolean);
    const models = listed?.length ? [...new Set(listed)] : DEFAULT_GEMINI_MODELS;
    return {
        provider: "gemini",
        apiKey: env.GEMINI_API_KEY,
        model: models[0],
        models,
        ...common,
    };
}
