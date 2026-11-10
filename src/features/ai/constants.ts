/** AI Creative Studio constants shared by server and browser (no dependencies). */

export const DESIGN_CATEGORIES = ["poetry", "line_art", "combination", "custom"] as const;
export type DesignCategory = (typeof DESIGN_CATEGORIES)[number];

export const TEXT_FONTS = ["script", "sans", "serif"] as const;
export type TextFont = (typeof TEXT_FONTS)[number];

/** Model font names → bundled single-line font ids (src/features/workspace/text/fonts). */
export const FONT_IDS: Record<TextFont, string> = {
    script: "HersheyScript1",
    sans: "HersheySans1",
    serif: "HersheySerifMed",
};

export const LIMITS = {
    promptMin: 3,
    promptMax: 500,
    refinementMax: 300,
    textLinesMax: 6,
    textLineMax: 80,
    svgMax: 500_000,
    titleMax: 80,
    descriptionMax: 240,
    questionMax: 200,
    suggestionsMax: 3,
    messageMax: 240,
} as const;

/** Blank margin kept on every side of the working area, in mm. */
export const MARGIN_MM = 10;
