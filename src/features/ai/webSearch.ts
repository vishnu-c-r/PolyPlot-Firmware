/**
 * Decides whether a request gets a web research step first (Google Search grounding
 * with Gemini, browser_search with Groq). Plain creative requests ("draw a flower",
 * "a poem about rain") never are; requests that lean on real-world,
 * current or factual information are.
 */

export function shouldOfferWebSearch(prompt: string, enabled: boolean): boolean {
    if (!enabled) return false;
    const trimmed = prompt.trim();
    // Skip only trivial abstract single-word shapes
    if (trimmed.split(/\s+/).length < 2 && /^(line|circle|square|star|triangle|box|shape|doodle)$/i.test(trimmed)) {
        return false;
    }
    return true;
}
