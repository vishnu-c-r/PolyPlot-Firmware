"use client";

import { useState, type ReactNode } from "react";
import { ArrowDown, ArrowLeft, ArrowRight, ArrowUp, CaretDown, CaretUp, House, LockOpen } from "@phosphor-icons/react";

import { Field, Input } from "@/components/ui/field";
import { Button } from "@/components/ui/button";
import { SegmentedControl } from "@/components/ui/segmented";
import { Tooltip } from "@/components/ui/tooltip";
import { cn } from "@/lib/utils";
import { usePlotter } from "../plotter/PlotterContext";

const STEPS = [0.1, 1, 10] as const;
type Step = (typeof STEPS)[number];
const STEP_OPTIONS = STEPS.map((value) => ({ value: String(value), label: String(value) }));
const DEFAULT_FEED = 10000;

/**
 * Live machine position and a jog pad. Jogging is relative (mm) at the chosen step;
 * the server holds any move with Z to the Z feed ceiling.
 */
export function MachinePanel() {
    const { telemetry, gatewayConnected, busy, jog, unlock, home } = usePlotter();
    const [step, setStep] = useState<Step>(1);
    const [feed, setFeed] = useState(DEFAULT_FEED);

    const live = gatewayConnected && telemetry !== null;
    const isAlarm = telemetry?.state === "Alarm";
    const canJog = live && !busy;
    const move = (axis: "x" | "y" | "z", direction: 1 | -1) => () => void jog({ [axis]: step * direction, feed });

    return (
        <div className="grid grid-cols-1 gap-4">
            <div>
                <dl className="grid grid-cols-3 overflow-hidden rounded-control bg-surface-sunken" aria-label="Machine position">
                    {(["X", "Y", "Z"] as const).map((axis, index) => (
                        <div key={axis} className="border-l border-line px-3 py-2.5 first:border-l-0">
                            <dt className="text-micro font-semibold text-subtle">{axis}</dt>
                            <dd className={cn("numeric mt-0.5 text-title leading-6", live ? "text-text" : "text-subtle")}>{live ? telemetry.mpos[index].toFixed(2) : "—"}</dd>
                        </div>
                    ))}
                </dl>
                <div className="mt-2 flex items-center justify-between text-caption text-muted">
                    <p>
                        Feed <span className="numeric text-text">{live ? telemetry.feed.toLocaleString() : "—"}</span> mm/min
                    </p>
                    <span className="font-mono text-micro text-subtle">
                        State: <span className={cn("font-semibold", isAlarm ? "text-danger" : "text-text")}>{live ? telemetry.state : "Offline"}</span>
                    </span>
                </div>
            </div>

            {/* Hardware Commands: Home Machine & Unlock */}
            <div className="grid grid-cols-2 gap-2">
                <Button
                    size="sm"
                    variant="secondary"
                    onClick={() => void home()}
                    disabled={!live || busy}
                    title="Run homing cycle on all axes ($H)"
                >
                    <House weight="bold" />
                    <span>Home Machine</span>
                </Button>
                <Button
                    size="sm"
                    variant={isAlarm ? "danger-solid" : "secondary"}
                    className={cn(isAlarm && "animate-pulse ring-2 ring-danger/40")}
                    onClick={() => void unlock()}
                    disabled={!live}
                    title="Unlock machine and clear Alarm state ($X)"
                >
                    <LockOpen weight="bold" />
                    <span>Unlock ($X)</span>
                </Button>
            </div>

            <div className="grid grid-cols-[1fr_auto] gap-2">
                <div className="grid grid-cols-[repeat(3,auto)] grid-rows-3 place-content-center gap-1 rounded-panel bg-surface-sunken p-1.5" role="group" aria-label="Jog X and Y">
                    <span />
                    <JogButton label={`Y +${step} mm`} disabled={!canJog} onClick={move("y", 1)}>
                        <ArrowUp weight="bold" />
                    </JogButton>
                    <span />
                    <JogButton label={`X −${step} mm`} disabled={!canJog} onClick={move("x", -1)}>
                        <ArrowLeft weight="bold" />
                    </JogButton>
                    <span className="numeric grid size-9 place-items-center text-micro text-subtle" aria-hidden>
                        {step}
                    </span>
                    <JogButton label={`X +${step} mm`} disabled={!canJog} onClick={move("x", 1)}>
                        <ArrowRight weight="bold" />
                    </JogButton>
                    <span />
                    <JogButton label={`Y −${step} mm`} disabled={!canJog} onClick={move("y", -1)}>
                        <ArrowDown weight="bold" />
                    </JogButton>
                    <span />
                </div>

                <div className="grid grid-rows-3 gap-1 rounded-panel bg-surface-sunken p-1.5" role="group" aria-label="Jog Z">
                    <JogButton label={`Z +${step} mm (pen up)`} disabled={!canJog} onClick={move("z", 1)}>
                        <CaretUp weight="bold" />
                    </JogButton>
                    <span className="grid size-9 place-items-center text-micro font-semibold text-subtle" aria-hidden>
                        Z
                    </span>
                    <JogButton label={`Z −${step} mm (pen down)`} disabled={!canJog} onClick={move("z", -1)}>
                        <CaretDown weight="bold" />
                    </JogButton>
                </div>

            </div>

            <div className="grid grid-cols-2 gap-3">
                    <div className="grid grid-cols-1 gap-1.5">
                        <span className="text-caption font-medium text-muted">
                            Step (mm)
                        </span>
                        <SegmentedControl
                            aria-label="Jog step in millimetres"
                            semantics="radio"
                            size="sm"
                            block
                            options={STEP_OPTIONS}
                            value={String(step)}
                            onChange={(value) => setStep(Number(value) as Step)}
                            className="[&_button]:numeric"
                        />
                    </div>
                    <Field label="XY feed (mm/min)">
                        {(props) => (
                            <Input
                                {...props}
                                type="number"
                                inputMode="numeric"
                                min={100}
                                max={20000}
                                step={100}
                                value={feed}
                                onChange={(event) => setFeed(Math.max(100, Math.min(20000, Number(event.target.value) || DEFAULT_FEED)))}
                                className="numeric h-8 px-2.5"
                            />
                        )}
                    </Field>
            </div>

            {!canJog && <p className="text-caption text-muted">{!live ? "Jogging is available once the plotter is connected." : "Jogging is paused while a plot is running."}</p>}
        </div>
    );
}

function JogButton({ label, disabled, onClick, children }: { label: string; disabled: boolean; onClick: () => void; children: ReactNode }) {
    return (
        <Tooltip label={label} side="top">
            <button
                type="button"
                aria-label={label}
                disabled={disabled}
                onClick={onClick}
                className={cn(
                    "focus-ring grid size-9 place-items-center rounded-[8px] bg-surface-raised text-text shadow-hairline [&_svg]:size-4",
                    "transition-[background-color,transform,color] duration-150 hover:not-disabled:bg-surface-hover active:not-disabled:scale-95 active:not-disabled:bg-accent-soft active:not-disabled:text-accent",
                    "disabled:cursor-not-allowed disabled:bg-transparent disabled:text-subtle disabled:shadow-none",
                )}
            >
                {children}
            </button>
        </Tooltip>
    );
}
