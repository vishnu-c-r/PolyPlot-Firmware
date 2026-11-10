"use client";

import { useRef, useState, type ChangeEvent, type ReactNode } from "react";
import { UploadSimple } from "@phosphor-icons/react";

import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover";
import { Spinner } from "@/components/ui/spinner";
import { Tooltip } from "@/components/ui/tooltip";
import { cn } from "@/lib/utils";
import {
    ILLUSTRATION_CATEGORIES,
    illustrationUrl,
    useIllustrations,
    useImportCustomArtwork,
    useInsertIllustration,
} from "@/features/illustrations/useIllustrations";
import type { Illustration } from "@/features/illustrations/library";
import { useEditor, type DrawingElement } from "../EditorContext";
import { SHAPES } from "./shapes";
import { ACTIVE_COLOR, IDLE_COLOR, toolButtonClass } from "./toolbar";

/**
 * Elements button: opens a panel with the drawing shapes, custom SVG import, and the
 * illustration library (served by /api/illustrations). Picking a shape arms the
 * Elements tool; picking an illustration places it in the middle of the page.
 */
export function ElementsMenu() {
    const { tool, element, setTool, setElement } = useEditor();
    const { library, load: loadLibrary, ensureLoaded } = useIllustrations();
    const { inserting, error: insertError, insert, clearError } = useInsertIllustration();
    const { importSvg, importing, error: importError, clearError: clearImportError } = useImportCustomArtwork();
    const [open, setOpen] = useState(false);
    const pickedRef = useRef(false);
    const fileInputRef = useRef<HTMLInputElement>(null);

    const active = tool === "Elements";
    const current = SHAPES.find((shape) => shape.element === element) ?? SHAPES[1];
    const highlighted = active || open;

    const onOpenChange = (next: boolean) => {
        if (next) ensureLoaded();
        clearError();
        clearImportError();
        setOpen(next);
    };

    const handleFileUpload = async (event: ChangeEvent<HTMLInputElement>) => {
        const file = event.target.files?.[0];
        if (!file) return;
        try {
            const text = await file.text();
            if (await importSvg(text)) {
                pickedRef.current = true;
                setOpen(false);
            }
        } catch (e) {
            console.error("Failed to read SVG file", e);
        } finally {
            if (fileInputRef.current) fileInputRef.current.value = "";
        }
    };

    const pickShape = (shape: DrawingElement) => {
        setElement(shape);
        setTool("Elements");
        pickedRef.current = true;
        setOpen(false);
    };

    const insertIllustration = async (illustration: Illustration) => {
        if (await insert(illustration)) {
            pickedRef.current = true;
            setOpen(false);
        }
    };

    return (
        <li>
            <Popover open={open} onOpenChange={onOpenChange}>
                <Tooltip label="Elements" side="right">
                    <PopoverTrigger asChild>
                        <button type="button" aria-label="Elements" className={toolButtonClass(highlighted)}>
                            {current.icon(highlighted ? ACTIVE_COLOR : IDLE_COLOR)}
                        </button>
                    </PopoverTrigger>
                </Tooltip>

                <PopoverContent
                    side="right"
                    align="start"
                    sideOffset={12}
                    aria-label="Elements"
                    tabIndex={-1}
                    // Focus the panel, not its first item, so no tooltip pops up on open.
                    onOpenAutoFocus={(event) => {
                        event.preventDefault();
                        (event.currentTarget as HTMLElement).focus();
                    }}
                    onCloseAutoFocus={(event) => {
                        if (pickedRef.current) event.preventDefault();
                        pickedRef.current = false;
                    }}
                    className="max-h-[70vh] w-76 overflow-y-auto overscroll-contain p-2"
                >
                    {/* Custom SVG Import / Upload with Minimal Pen Lifts */}
                    <div className="mb-2 px-1">
                        <input
                            ref={fileInputRef}
                            type="file"
                            accept=".svg,image/svg+xml"
                            className="hidden"
                            onChange={handleFileUpload}
                        />
                        <button
                            type="button"
                            onClick={() => fileInputRef.current?.click()}
                            disabled={importing}
                            className="focus-ring flex w-full items-center justify-center gap-2 rounded-control border border-dashed border-accent/40 bg-accent-soft/30 py-2 font-sans text-caption font-semibold text-accent transition-colors hover:bg-accent-soft/60 disabled:opacity-50"
                        >
                            <UploadSimple size={15} weight="bold" />
                            <span>{importing ? "Optimizing & Importing…" : "Import Custom SVG"}</span>
                        </button>
                        {importError && <p className="mt-1 text-center text-micro text-danger">{importError}</p>}
                    </div>

                    <PanelSection title="Shapes">
                        <div className="grid grid-cols-4 gap-1" role="radiogroup" aria-label="Shapes">
                            {SHAPES.map((shape) => {
                                const selected = active && element === shape.element;
                                return (
                                    <Tooltip key={shape.element} label={shape.label}>
                                        <button
                                            type="button"
                                            role="radio"
                                            aria-checked={selected}
                                            aria-label={shape.label}
                                            onClick={() => pickShape(shape.element)}
                                            className={cn(
                                                "focus-ring grid h-12 place-items-center rounded-[10px] transition-colors duration-150",
                                                selected ? "bg-accent-soft text-accent" : "text-muted hover:bg-surface-hover hover:text-text",
                                            )}
                                        >
                                            {shape.icon(ACTIVE_COLOR)}
                                        </button>
                                    </Tooltip>
                                );
                            })}
                        </div>
                    </PanelSection>

                    {library.status === "loading" && (
                        <p className="flex items-center justify-center gap-2 py-6 text-caption text-muted">
                            <Spinner /> Loading illustrations…
                        </p>
                    )}

                    {library.status === "error" && (
                        <p className="py-6 text-center text-caption text-muted">
                            Couldn&apos;t load illustrations.{" "}
                            <button type="button" onClick={() => void loadLibrary()} className="focus-ring rounded-[4px] font-semibold text-accent underline underline-offset-2">
                                Retry
                            </button>
                        </p>
                    )}

                    {library.status === "ready" &&
                        ILLUSTRATION_CATEGORIES.map(({ id, label }) => {
                            const items = library.illustrations.filter((item) => item.category === id);
                            if (!items.length) return null;
                            return (
                                <PanelSection key={id} title={label}>
                                    <div className="grid grid-cols-4 gap-1">
                                        {items.map((item) => (
                                             <Tooltip key={item.id} label={item.name}>
                                                <button
                                                    type="button"
                                                    aria-label={`Add ${item.name}`}
                                                    disabled={inserting !== null}
                                                    onClick={() => void insertIllustration(item)}
                                                    className="focus-ring grid h-16 place-items-center rounded-[10px] bg-paper p-1.5 shadow-hairline transition-[transform,box-shadow] duration-150 hover:not-disabled:-translate-y-px hover:not-disabled:shadow-raised disabled:opacity-50"
                                                >
                                                    {/* eslint-disable-next-line @next/next/no-img-element -- small SVG thumbnails from our own API */}
                                                    <img src={illustrationUrl(item.id)} alt="" className="size-11 object-contain" />
                                                </button>
                                            </Tooltip>
                                        ))}
                                    </div>
                                </PanelSection>
                            );
                        })}

                    {insertError && (
                        <p role="alert" className="mx-1 mt-2 text-caption text-danger">
                            {insertError}
                        </p>
                    )}
                </PopoverContent>
            </Popover>
        </li>
    );
}

function PanelSection({ title, children }: { title: string; children: ReactNode }) {
    return (
        <section className="p-1 pb-2 last:pb-1">
            <h3 className="mb-1.5 px-1 text-caption font-semibold text-muted">{title}</h3>
            {children}
        </section>
    );
}
