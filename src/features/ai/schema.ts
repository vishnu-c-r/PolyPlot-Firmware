import { z } from "zod";

/**
 * Contracts for the AI Creative Studio: what the model must return, what the browser
 * may send, and what the API answers. Shared by server and client (no secrets here).
 */

import { DESIGN_CATEGORIES, LIMITS, TEXT_FONTS, type DesignCategory } from "./constants";

export { DESIGN_CATEGORIES, FONT_IDS, LIMITS, TEXT_FONTS, type DesignCategory, type TextFont } from "./constants";

/** A rectangle in mm within the working area. */
const BoxSchema = z.object({
    x: z.number().finite(),
    y: z.number().finite(),
    width: z.number().finite().positive(),
    height: z.number().finite().positive(),
});
export type Box = z.infer<typeof BoxSchema>;

const TextSchema = z.object({
    // Models sometimes add blank spacer lines (e.g. between a quote and its author);
    // they're dropped before the count and length checks.
    lines: z
        .array(z.string())
        .transform((lines) => lines.map((line) => line.trim()).filter(Boolean))
        .pipe(z.array(z.string().max(LIMITS.textLineMax)).min(1).max(LIMITS.textLinesMax)),
    font: z.enum(TEXT_FONTS),
    box: BoxSchema,
});
export type DesignText = z.infer<typeof TextSchema>;

const IllustrationSchema = z.object({
    svg: z.string().min(1).max(LIMITS.svgMax),
    box: BoxSchema,
});

/** What the model must return (validated after the provider's own structured output). */
export const ModelDesignSchema = z
    .object({
        status: z.enum(["success", "needs_clarification", "error"]),
        title: z.string().trim().max(LIMITS.titleMax).nullable(),
        description: z.string().trim().max(LIMITS.descriptionMax).nullable(),
        category: z.enum(DESIGN_CATEGORIES).nullable(),
        text: TextSchema.nullable(),
        illustration: IllustrationSchema.nullable(),
        clarification: z
            .object({
                question: z.string().trim().min(1).max(LIMITS.questionMax),
                suggestions: z.array(z.string().trim().min(1).max(LIMITS.promptMax)).max(LIMITS.suggestionsMax),
            })
            .nullable(),
        message: z.string().trim().max(LIMITS.messageMax).nullable(),
    })
    .superRefine((design, ctx) => {
        const issue = (message: string) => ctx.addIssue({ code: "custom", message });
        if (design.status === "needs_clarification" && !design.clarification) issue("clarification missing");
        if (design.status !== "success") return;
        if (!design.title || !design.category) issue("title and category are required");
        if (design.category === "poetry" && !design.text) issue("poetry needs text");
        if (design.category === "line_art" && !design.illustration) issue("line art needs an illustration");
        if (design.category === "combination" && (!design.text || !design.illustration)) issue("combination needs text and an illustration");
        if (design.category === "custom" && !design.text && !design.illustration) issue("custom needs text or an illustration");
    });
export type ModelDesign = z.infer<typeof ModelDesignSchema>;

/**
 * The same contract as JSON Schema for structured output (Gemini's response_format and
 * Groq's strict json_schema). Written by hand to stay within both documented subsets (every property required, nullable via a
 * "null" union, additionalProperties false). Length and count limits are left to
 * ModelDesignSchema, which re-checks everything.
 */
const nullable = (schema: Record<string, unknown>) => ({ anyOf: [schema, { type: "null" }] });
const boxJson = {
    type: "object",
    properties: {
        x: { type: "number", description: "Left edge in mm" },
        y: { type: "number", description: "Top edge in mm" },
        width: { type: "number", description: "Width in mm" },
        height: { type: "number", description: "Height in mm" },
    },
    required: ["x", "y", "width", "height"],
    additionalProperties: false,
};

export const MODEL_DESIGN_JSON_SCHEMA = {
    type: "object",
    properties: {
        status: { type: "string", enum: ["success", "needs_clarification", "error"] },
        title: nullable({ type: "string" }),
        description: nullable({ type: "string" }),
        category: nullable({ type: "string", enum: [...DESIGN_CATEGORIES] }),
        text: nullable({
            type: "object",
            properties: {
                lines: { type: "array", items: { type: "string" } },
                font: { type: "string", enum: [...TEXT_FONTS] },
                box: boxJson,
            },
            required: ["lines", "font", "box"],
            additionalProperties: false,
        }),
        illustration: nullable({
            type: "object",
            properties: {
                svg: { type: "string", description: "Complete <svg> document with stroke-only line geometry" },
                box: boxJson,
            },
            required: ["svg", "box"],
            additionalProperties: false,
        }),
        clarification: nullable({
            type: "object",
            properties: {
                question: { type: "string" },
                suggestions: { type: "array", items: { type: "string" } },
            },
            required: ["question", "suggestions"],
            additionalProperties: false,
        }),
        message: nullable({ type: "string" }),
    },
    required: ["status", "title", "description", "category", "text", "illustration", "clarification", "message"],
    additionalProperties: false,
} as const;

/** A design as the browser holds it (also sent back for refinement). */
const PreviousDesignSchema = z.object({
    title: z.string().max(LIMITS.titleMax),
    category: z.enum(DESIGN_CATEGORIES),
    text: TextSchema.nullable(),
    illustration: z.object({ svg: z.string().max(LIMITS.svgMax), box: BoxSchema }).nullable(),
});

export const DesignRequestSchema = z
    .object({
        mode: z.enum(["generate", "refine"]),
        prompt: z.string().trim().min(LIMITS.promptMin).max(LIMITS.promptMax),
        category: z.enum([...DESIGN_CATEGORIES, "auto"]).default("auto"),
        refinement: z.string().trim().min(1).max(LIMITS.refinementMax).optional(),
        previous: PreviousDesignSchema.optional(),
    })
    .strict()
    .superRefine((request, ctx) => {
        if (request.mode === "refine" && (!request.refinement || !request.previous)) {
            ctx.addIssue({ code: "custom", message: "refine needs refinement and previous" });
        }
    });
export type DesignRequest = z.infer<typeof DesignRequestSchema>;

/** A validated design returned to the browser. */
export interface Design {
    title: string;
    description: string | null;
    category: DesignCategory;
    /** Text to letter with the single-line fonts; `fontId` is a bundled font id. */
    text: (DesignText & { fontId: string }) | null;
    /** Sanitized SVG sized to the full working area, plus where the drawing sits in it. */
    illustration: { svg: string; box: Box } | null;
    layout: { width: number; height: number; unit: "mm" };
}

export interface Source {
    url: string;
    title?: string;
}

export type DesignResponse =
    | { status: "success"; design: Design; sources: Source[]; usedWebSearch: boolean }
    | { status: "needs_clarification"; clarification: { question: string; suggestions: string[] }; usedWebSearch: boolean }
    | { status: "error"; error: string };
