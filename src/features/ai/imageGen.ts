import "server-only";

export class ImageGenError extends Error {
    constructor(message: string, readonly status: number = 502) {
        super(message);
        this.name = "ImageGenError";
    }
}

/**
 * Uses our configured LLM API (xAI Grok, Groq Cloud, or Gemini) to rewrite the user prompt
 * into an optimal line-art prompt before passing to FLUX.
 */
export async function optimizePromptWithLLM(userPrompt: string, apiKey: string): Promise<string> {
    const systemPrompt =
        "You are an expert prompt engineer for pen plotter line art and FLUX diffusion models. " +
        "Rewrite the user drawing request into an optimized prompt for generating ultra-clean, " +
        "minimalist black line drawings on pure white backgrounds. " +
        "Rules:\n" +
        "- Specify clean closed outer contours, coloring book style, and crisp pen plotter line art.\n" +
        "- Default to 90-degree lateral side profile silhouette unless user explicitly requested another angle.\n" +
        "- Strictly forbid ground lines, floor lines, roads, horizons, shadows, shading, hatching, and noise.\n" +
        "- Keep the prompt under 35 words.\n" +
        "- Return ONLY the rewritten prompt string, no explanations, no quotes.";

    // 1. Check for dedicated xAI Grok API key if present
    const xaiKey = process.env.XAI_API_KEY || process.env.GROK_API_KEY;
    if (xaiKey) {
        try {
            const controller = new AbortController();
            const timeout = setTimeout(() => controller.abort(), 4000);
            const res = await fetch("https://api.x.ai/v1/chat/completions", {
                method: "POST",
                headers: { "Content-Type": "application/json", Authorization: `Bearer ${xaiKey}` },
                body: JSON.stringify({
                    model: "grok-2",
                    messages: [
                        { role: "system", content: systemPrompt },
                        { role: "user", content: userPrompt },
                    ],
                    max_tokens: 60,
                }),
                signal: controller.signal,
            });
            clearTimeout(timeout);
            if (res.ok) {
                const data = await res.json();
                const text = data?.choices?.[0]?.message?.content?.trim();
                if (text) {
                    console.info("[AI] Grok optimized prompt:", text);
                    return text.replace(/^["']|["']$/g, "");
                }
            }
        } catch (e) {
            console.warn("[AI] xAI Grok prompt optimization skipped:", e);
        }
    }

    // 2. Check for Groq Cloud API key (gsk_...)
    if (apiKey && apiKey.startsWith("gsk_")) {
        try {
            const controller = new AbortController();
            const timeout = setTimeout(() => controller.abort(), 4000);
            const res = await fetch("https://api.groq.com/openai/v1/chat/completions", {
                method: "POST",
                headers: { "Content-Type": "application/json", Authorization: `Bearer ${apiKey}` },
                body: JSON.stringify({
                    model: "llama-3.3-70b-versatile",
                    messages: [
                        { role: "system", content: systemPrompt },
                        { role: "user", content: userPrompt },
                    ],
                    max_tokens: 60,
                }),
                signal: controller.signal,
            });
            clearTimeout(timeout);
            if (res.ok) {
                const data = await res.json();
                const text = data?.choices?.[0]?.message?.content?.trim();
                if (text) {
                    console.info("[AI] Groq optimized prompt:", text);
                    return text.replace(/^["']|["']$/g, "");
                }
            }
        } catch (e) {
            console.warn("[AI] Groq prompt optimization skipped:", e);
        }
    }

    // 3. Check for Google Generative AI / Gemini API key (AQ... or AIza...)
    if (apiKey && (apiKey.startsWith("AQ.") || apiKey.startsWith("AIza"))) {
        try {
            const controller = new AbortController();
            const timeout = setTimeout(() => controller.abort(), 4000);
            const res = await fetch(
                `https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-lite-latest:generateContent?key=${apiKey}`,
                {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({
                        contents: [{ parts: [{ text: `${systemPrompt}\n\nUser request: ${userPrompt}` }] }],
                    }),
                    signal: controller.signal,
                }
            );
            clearTimeout(timeout);
            if (res.ok) {
                const data = await res.json();
                const text = data?.candidates?.[0]?.content?.parts?.[0]?.text?.trim();
                if (text) {
                    console.info("[AI] Our API optimized prompt:", text);
                    return text.replace(/^["']|["']$/g, "");
                }
            }
        } catch (e) {
            console.warn("[AI] Gemini prompt optimization skipped:", e);
        }
    }

    // Fallback: rule-based template
    const hasExplicitView = /view|profile|portrait|front|top|facing/i.test(userPrompt);
    const viewGuidance = hasExplicitView ? "" : "side profile, lateral view from the side, ";
    return `clean minimalist black line drawing of ${userPrompt}, ${viewGuidance}simple outline, coloring book style, pure white background, isolated in center, completely NO ground line, NO floor line, NO road, NO horizon, NO shadows, NO hatch marks, NO interior shading, zero clutter, clean pen plotter line art`;
}

/**
 * Generates a clean line art raster image using Google Nano Banana or OpenAI DALL-E.
 */
export async function generateLineArtImage(prompt: string, apiKey: string): Promise<Buffer> {
    const formattedPrompt = await optimizePromptWithLLM(prompt, apiKey);

    // 1. Pollinations AI (FLUX)
    const pollinationsKey = process.env.POLLINATIONS_API_KEY || (apiKey.startsWith("sk_") ? apiKey : null);
    if (pollinationsKey) {
        try {
            console.info("[AI] Generating line art image via Pollinations FLUX...");
            const seed = Math.floor(Math.random() * 1000000);
            const url = `https://gen.pollinations.ai/image/${encodeURIComponent(formattedPrompt)}?width=1024&height=432&model=flux&nologo=true&seed=${seed}`;
            const resp = await fetch(url, {
                headers: {
                    Authorization: `Bearer ${pollinationsKey}`,
                    "User-Agent": "PlotterStudio/1.0",
                },
            });
            if (resp.ok) {
                const arrayBuffer = await resp.arrayBuffer();
                return Buffer.from(arrayBuffer);
            } else {
                console.warn(`[AI] Pollinations returned ${resp.status}`);
            }
        } catch (e) {
            console.warn("[AI] Pollinations image gen error:", e);
        }
    }

    // 2. If an OpenAI key is present, try DALL-E 3
    if (process.env.OPENAI_API_KEY) {
        try {
            const resp = await fetch("https://api.openai.com/v1/images/generations", {
                method: "POST",
                headers: {
                    "Content-Type": "application/json",
                    Authorization: `Bearer ${process.env.OPENAI_API_KEY}`,
                },
                body: JSON.stringify({
                    model: "dall-e-3",
                    prompt: formattedPrompt,
                    n: 1,
                    size: "1024x1024",
                    response_format: "b64_json",
                }),
            });
            if (resp.ok) {
                const data = await resp.json();
                const b64 = data.data?.[0]?.b64_json;
                if (b64) return Buffer.from(b64, "base64");
            }
        } catch (e) {
            console.warn("[AI] OpenAI image gen failed:", e);
        }
    }

    // 2. Google Nano Banana via Gemini API
    const models = ["gemini-2.5-flash-image", "nano-banana-pro-preview", "gemini-3.1-flash-image"];
    for (const model of models) {
        const url = `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${apiKey}`;
        const payload = {
            contents: [{ parts: [{ text: formattedPrompt }] }],
            generationConfig: { responseModalities: ["IMAGE"] },
        };

        const resp = await fetch(url, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
        });

        if (resp.ok) {
            const data = await resp.json();
            const parts = data?.candidates?.[0]?.content?.parts ?? [];
            for (const part of parts) {
                if (part.inlineData?.data) {
                    return Buffer.from(part.inlineData.data, "base64");
                }
            }
        }

        if (resp.status === 429) {
            throw new ImageGenError(
                "Nano Banana quota exceeded. Google AI Studio requires a linked Google Cloud Billing account for image generation. Please enable billing at aistudio.google.com or provide an OpenAI key in .env.",
                429
            );
        }
    }

    throw new ImageGenError("Failed to generate line art image from provider.", 502);
}

