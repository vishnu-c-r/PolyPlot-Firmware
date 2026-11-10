import "server-only";

import { AiConfigError, getAiConfig, type AiConfig } from "./config";
import { generateLineArtImage } from "./imageGen";
import { buildInput, buildInstructions, buildRepairInput } from "./prompts";
import { createGeminiProvider } from "./providers/gemini";
import { createGroqProvider } from "./providers/groq";
import { DesignError, FAILED, type DesignProvider } from "./providers/shared";
import { fitBox, sanitizeSvg } from "./sanitizeSvg";
import { FONT_IDS, type Design, type DesignRequest, type DesignResponse, type ModelDesign } from "./schema";
import { traceImageToSvg } from "./vectorize";
import { shouldOfferWebSearch } from "./webSearch";

/** The provider chosen by AI_PROVIDER (gemini by default). */
function createProvider(config: AiConfig): DesignProvider {
    return config.provider === "groq" ? createGroqProvider(config) : createGeminiProvider(config);
}

function toDesign(model: ModelDesign, config: AiConfig, illustration: Design["illustration"]): Design {
    const text = model.text
        ? {
            lines: model.text.lines.map((line) => line.trim()).filter(Boolean),
            font: model.text.font,
            fontId: FONT_IDS[model.text.font],
            box: fitBox(config.area, model.text.box),
        }
        : null;

    return {
        title: model.title!,
        description: model.description,
        category: model.category!,
        text: text && text.lines.length ? text : null,
        illustration,
        layout: { width: config.area.width, height: config.area.height, unit: "mm" },
    };
}

function mapProviderError(error: unknown, provider: DesignProvider | null): DesignError {
    if (error instanceof DesignError) return error;
    if (error instanceof AiConfigError) {
        console.warn(`[AI] ${error.message}`);
        return new DesignError("AI isn't set up on this server yet.", 503);
    }
    const mapped = provider?.mapError(error);
    if (mapped) return mapped;
    console.warn("[AI] Unexpected error:", error instanceof Error ? error.name : typeof error);
    return new DesignError(FAILED, 502);
}

/**
 * Generates (or refines) a design: an optional web research step, one structured call
 * to the configured provider (Gemini or Groq), validation, and SVG sanitization, with
 * one repair attempt if the SVG is rejected. Returns the response body and the HTTP status to send.
 */
export async function generateDesign(request: DesignRequest): Promise<{ body: DesignResponse; status: number }> {
    const ok = (body: DesignResponse) => ({ body, status: 200 });
    let provider: DesignProvider | null = null;
    try {
        const config = getAiConfig();

        // A refined design's SVG comes back from the browser: check it again before use.
        let previousSvg: string | null = null;
        if (request.mode === "refine" && request.previous?.illustration) {
            const checked = sanitizeSvg(request.previous.illustration.svg, config.area, request.previous.illustration.box);
            if (!checked.ok) throw new DesignError("The previous design couldn't be read. Start a new design.", 400);
            previousSvg = checked.svg;
        }

        provider = createProvider(config);
        const found = shouldOfferWebSearch(request.prompt, config.webSearchEnabled) ? await provider.research(request.prompt) : null;
        const sources = found?.sources ?? [];
        const usedWebSearch = found !== null;

        const category = request.mode === "refine" ? request.previous!.category : request.category;

        // Route A: If generating line art, attempt native AI image generation + vector tracing (Potrace)
        if (request.mode === "generate" && (category === "line_art" || category === "auto")) {
            try {
                console.info("[AI] Attempting Route A: AI Image Generation + Vector Trace...");
                const imgBuffer = await generateLineArtImage(request.prompt, config.apiKey);
                const rawSvg = await traceImageToSvg(imgBuffer);
                const sanitized = sanitizeSvg(rawSvg, config.area, null);
                if (sanitized.ok) {
                    const illustration = { svg: sanitized.svg, box: sanitized.box };
                    const autoDesign: ModelDesign = {
                        status: "success",
                        title: request.prompt.slice(0, 32),
                        description: `Clean vector line art of ${request.prompt}`,
                        category: "line_art",
                        text: null,
                        clarification: null,
                        message: null,
                        illustration,
                    };
                    return ok({ status: "success", design: toDesign(autoDesign, config, illustration), sources: [], usedWebSearch: false });
                } else {
                    console.warn("[AI] Route A trace rejected by sanitizer:", sanitized.reasons);
                }
            } catch (err) {
                console.warn("[AI] Route A image generation / trace bypassed:", err instanceof Error ? err.message : err);
            }
        }

        const instructions = buildInstructions(config.area, category, Boolean(found?.notes));
        const input = buildInput(request, previousSvg, found?.notes || null);

        let design = await provider.design(instructions, input);

        for (let attempt = 0; ; attempt++) {
            if (design.status === "needs_clarification") {
                return ok({ status: "needs_clarification", clarification: design.clarification!, usedWebSearch });
            }
            if (design.status === "error") {
                return {
                    body: { status: "error", error: design.message || "That isn't something the studio can create. Try a different idea." },
                    status: 422,
                };
            }

            if (!design.illustration) {
                return ok({ status: "success", design: toDesign(design, config, null), sources, usedWebSearch });
            }

            // Without text the drawing gets the whole safe area (models tend to pick tiny
            // boxes); with text, the model's layout keeps the two apart.
            const target = design.text ? design.illustration.box : null;
            const sanitized = sanitizeSvg(design.illustration.svg, config.area, target);
            if (sanitized.ok) {
                const illustration = { svg: sanitized.svg, box: sanitized.box };
                return ok({ status: "success", design: toDesign(design, config, illustration), sources, usedWebSearch });
            }

            console.warn(`[AI] SVG rejected (${sanitized.reasons.join("; ")})${attempt === 0 ? ", retrying" : ""}`);
            if (attempt > 0) throw new DesignError("The drawing couldn't be made plotter-safe. Try again or simplify the idea.", 502);
            design = await provider.design(instructions, buildRepairInput(input, sanitized.reasons));
        }
    } catch (error) {
        const failure = mapProviderError(error, provider);
        return { body: { status: "error", error: failure.message }, status: failure.status };
    }
}
