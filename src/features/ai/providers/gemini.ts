import "server-only";

import { GoogleGenAI, type GoogleGenAIInteraction, type Interactions } from "@google/genai";

import type { AiConfig } from "../config";
import { RESEARCH_INSTRUCTIONS } from "../prompts";
import { MODEL_DESIGN_JSON_SCHEMA, type ModelDesign } from "../schema";
import {
    DesignError,
    FAILED,
    RESEARCH_MAX_TOKENS,
    RESEARCH_TIMEOUT_MS,
    SourceList,
    TOO_DETAILED,
    TOO_SLOW,
    errorForStatus,
    parseDesign,
    type DesignProvider,
    type Research,
} from "./shared";

type Interaction = GoogleGenAIInteraction;

/**
 * Total time for one design call across the fallback models. The browser waits 180 s,
 * which leaves room for the occasional repair call.
 */
const DESIGN_BUDGET_MS = 120_000;
/** A model that isn't the last gets at most this long before the next one is tried. */
const FALLBACK_ATTEMPT_MS = 45_000;
type CreateParams = Interactions.InteractionCreateParams.CreateModelInteractionParamsNonStreaming;

/** Page titles and URLs that Google Search grounding cites in the reply text. */
function collectSources(interaction: Interaction) {
    const sources = new SourceList();
    for (const step of interaction.steps ?? []) {
        if (step.type !== "model_output") continue;
        for (const content of step.content ?? []) {
            if (content.type !== "text") continue;
            for (const annotation of content.annotations ?? []) {
                if (annotation.type === "url_citation") sources.add(annotation.url, annotation.title);
            }
        }
    }
    return sources.list();
}

/** With store: false Gemini returns no interaction id, so logs only mention one when present. */
const idNote = (interaction: Interaction) => (interaction.id ? `, id ${interaction.id}` : "");

/** Throws unless the interaction finished; `incomplete` means it hit max_output_tokens. */
function checkFinished(interaction: Interaction, what: string) {
    if (interaction.status === "completed") return;
    if (interaction.status === "incomplete") {
        const usage = interaction.usage;
        console.warn(`[AI] Gemini ${what} hit the token limit (${usage?.total_output_tokens ?? "?"} out, ${usage?.total_thought_tokens ?? 0} thinking)${idNote(interaction)}`);
        throw new DesignError(TOO_DETAILED, 502);
    }
    const codes = ((interaction.errors ?? []) as Array<{ code?: string }>).map((error) => error.code).filter(Boolean).join(", ");
    console.warn(`[AI] Gemini ${what} ended with status ${interaction.status}${codes ? ` (${codes})` : ""}${idNote(interaction)}`);
    throw new DesignError(FAILED, 502);
}

function logUsage(interaction: Interaction, what: string) {
    const usage = interaction.usage;
    if (usage) console.info(`[AI] Gemini ${what} tokens: ${usage.total_input_tokens ?? "?"} in, ${usage.total_output_tokens ?? "?"} out, ${usage.total_thought_tokens ?? 0} thinking${idNote(interaction)}`);
}

/** Timeouts from the SDK (class names are minified in production builds). */
const isTimeout = (error: unknown) => error instanceof Error && (/Timeout/i.test(error.name) || /timed out/i.test(error.message));

/** HTTP status on the Interactions API's errors (their classes aren't exported). */
function statusOf(error: unknown): number | undefined {
    if (!error || typeof error !== "object") return undefined;
    const { status, statusCode } = error as { status?: unknown; statusCode?: unknown };
    if (typeof status === "number") return status;
    if (typeof statusCode === "number") return statusCode;
    return undefined;
}

/** Google Gemini through the Interactions API (@google/genai). */
export function createGeminiProvider(config: AiConfig): DesignProvider {
    const client = new GoogleGenAI({ apiKey: config.apiKey });

    /**
     * Calls the models in config.models in order within `budget` ms. A model that is busy
     * (503), out of requests (429, e.g. its Free Tier daily limit), not served (404) or
     * slow (no answer within FALLBACK_ATTEMPT_MS) hands over to the next; the last model gets whatever
     * time is left. Any other error is thrown. store: false keeps prompts and designs
     * off Google's interaction history.
     */
    const create = async (params: Omit<CreateParams, "model" | "store" | "stream">, budget: number): Promise<Interaction> => {
        const deadline = Date.now() + budget;
        for (const [index, model] of config.models.entries()) {
            const last = index === config.models.length - 1;
            const remaining = deadline - Date.now();
            if (remaining < 5_000) throw new DesignError(TOO_SLOW, 504);
            const request: CreateParams = { ...params, model, store: false, stream: false };
            try {
                return await client.interactions.create(request, {
                    timeout_ms: last ? remaining : Math.min(remaining, FALLBACK_ATTEMPT_MS),
                    // The SDK's default retry backs off on 429/5xx for up to an hour. Fail
                    // fast while another model is left; one quick retry on the last.
                    retries: last
                        ? { strategy: "attempt-count-backoff", maxRetries: 1, backoff: { initialInterval: 2000, maxInterval: 4000 }, retryConnectionErrors: false }
                        : { strategy: "none" },
                });
            } catch (error) {
                const status = statusOf(error);
                const reason =
                    status === 503 ? "is busy (503)"
                    : status === 429 ? "is out of requests (429)"
                    : status === 404 ? "isn't available (404)"
                    : isTimeout(error) ? "is too slow"
                    : null;
                if (last || !reason) throw error;
                console.warn(`[AI] ${model} ${reason}; trying ${config.models[index + 1]}`);
            }
        }
        throw new Error("No Gemini model configured");
    };

    /**
     * Research is its own call (Google Search grounding, plain-text notes), the same flow
     * as Groq. Gemini 3 can combine search with structured output in one call, but that
     * is still a preview feature.
     */
    async function research(prompt: string): Promise<Research | null> {
        let interaction: Interaction;
        try {
            interaction = await create(
                {
                    system_instruction: RESEARCH_INSTRUCTIONS,
                    input: prompt,
                    tools: [{ type: "google_search" }],
                    generation_config: { thinking_level: "low", max_output_tokens: RESEARCH_MAX_TOKENS },
                },
                RESEARCH_TIMEOUT_MS,
            );
        } catch (error) {
            // Google Search grounding has its own quota, and some plans (the Free Tier)
            // have none: every search call answers 429 "exceeded your current quota"
            // while plain calls work. Design without research rather than fail.
            if (statusOf(error) === 429 && error instanceof Error && /quota/i.test(error.message)) {
                console.warn("[AI] Google Search grounding isn't available on this API key's quota; designing without web research.");
                return null;
            }
            throw error;
        }
        checkFinished(interaction, "research");
        logUsage(interaction, "research");

        const sources = collectSources(interaction);
        const notes = (interaction.output_text ?? "").trim();
        if (!sources.length && !notes) throw new DesignError("Web search didn't return anything usable. Try again.", 502);
        return { notes, sources };
    }

    async function design(instructions: string, input: string): Promise<ModelDesign> {
        const interaction = await create(
            {
                system_instruction: instructions,
                input,
                response_format: { type: "text", mime_type: "application/json", schema: MODEL_DESIGN_JSON_SCHEMA },
                // Drawing well takes planning; "high" gives clearly better line art.
                generation_config: { thinking_level: "high", max_output_tokens: config.maxOutputTokens },
            },
            DESIGN_BUDGET_MS,
        );
        checkFinished(interaction, "design");
        logUsage(interaction, "design");
        return parseDesign(interaction.output_text, interaction.id);
    }

    function mapError(error: unknown): DesignError | null {
        if (!(error instanceof Error)) return null;
        if (isTimeout(error)) return new DesignError(TOO_SLOW, 504);
        const status = statusOf(error);
        if (status === undefined) {
            if (/Connection|Abort|HTTPClient|ContinuationInterrupted/i.test(error.name)) {
                console.warn(`[AI] Gemini request failed (${error.name})`);
                return new DesignError(FAILED, 502);
            }
            return null;
        }
        console.warn(`[AI] Gemini error ${status} (${error.name})`);
        // The message is only inspected here, never shown: Gemini reports a bad key as
        // 400, and a used-up daily quota (Free Tier) as 429 "requests per day".
        // (@google/genai 3.0 can't read the array-shaped error body Google sends for a bad
        // key, so the message may be empty; the log hint covers that case.)
        if (status === 400 && /api[ _-]?key/i.test(error.message)) return errorForStatus(401, "GEMINI_MODEL");
        if (status === 400) console.warn("[AI] Gemini rejected the request (400). An invalid GEMINI_API_KEY also shows up this way.");
        if (status === 429 && /per day/i.test(error.message)) {
            return new DesignError("The AI service's daily limit is used up. Try again tomorrow, or raise the limit in Google AI Studio.", 429);
        }
        return errorForStatus(status, "GEMINI_MODEL");
    }

    return { name: "gemini", research, design, mapError };
}
