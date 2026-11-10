"use client";

import { useEffect, useState } from "react";
import { Pause, Play, Stop, X } from "@phosphor-icons/react";

import { Button } from "@/components/ui/button";
import { Progress } from "@/components/ui/progress";
import { StatusDot, type Tone } from "@/components/ui/status";
import { cn } from "@/lib/utils";
import { usePlotter } from "./PlotterContext";

import { formatDuration } from "./format";

/** Plotter connection as a dot and a word. */
export function PlotterStatus({ className = "" }: { className?: string }) {
    const { link, gatewayConnected, telemetry, unlock } = usePlotter();

    const [tone, label]: [Tone, string] =
        link !== "online"
            ? ["neutral", link === "connecting" ? "Connecting…" : "App offline"]
            : !gatewayConnected
                ? ["danger", "Plotter offline"]
                : telemetry?.state === "Alarm"
                    ? ["warn", "Alarm"]
                    : ["ok", telemetry?.state ?? "Online"];

    return (
        <span className={cn("inline-flex items-center gap-1.5 text-caption text-muted", className)}>
            <StatusDot tone={tone} live={link !== "online"} />
            {label}
            {gatewayConnected && telemetry?.state === "Alarm" && (
                <button
                    type="button"
                    onClick={(e) => {
                        e.stopPropagation();
                        void unlock();
                    }}
                    className="ml-1 rounded bg-amber-500/15 px-1.5 py-0.5 font-mono text-[10px] font-semibold text-amber-700 hover:bg-amber-500/25 active:scale-95 transition-all"
                    title="Clear Alarm ($X)"
                >
                    Unlock ($X)
                </button>
            )}
        </span>
    );
}

/**
 * Progress and controls for the current plot: state, progress bar, line count, time
 * left, live X/Y/Z, Hold/Resume and Abort (which asks for a second tap).
 * Renders nothing when no job is running or waiting to be dismissed.
 */
export function PlotterHud({
    compact = false,
    showPosition = !compact,
    showStatus = true,
    className = "",
}: {
    compact?: boolean;
    /** Live X/Y/Z under the progress (off where a machine panel shows it). */
    showPosition?: boolean;
    /** Connection dot next to the title (off where a header shows it). */
    showStatus?: boolean;
    className?: string;
}) {
    const { job, progress, telemetry, held, hold, resume, abort, dismiss } = usePlotter();
    const [confirmAbort, setConfirmAbort] = useState(false);

    useEffect(() => {
        if (!confirmAbort) return;
        const timeout = setTimeout(() => setConfirmAbort(false), 3000);
        return () => clearTimeout(timeout);
    }, [confirmAbort]);

    if (!job) return null;

    const running = job.phase === "running";
    const percent = running ? (progress?.percent ?? 0) : (job.percent ?? (job.phase === "done" ? 100 : 0));
    const [title, tone]: [string, Tone] = running
        ? held
            ? ["Paused", "warn"]
            : ["Plotting", "accent"]
        : job.phase === "done"
            ? ["Plot finished", "ok"]
            : job.phase === "aborted"
                ? ["Plot aborted", "neutral"]
                : ["Plot failed", "danger"];

    const onAbort = () => {
        if (!confirmAbort) {
            setConfirmAbort(true);
            return;
        }
        setConfirmAbort(false);
        void abort();
    };

    return (
        <section aria-label="Plot progress" className={cn("grid grid-cols-1 gap-2.5", className)}>
            <div className="flex items-center justify-between gap-2">
                <div className="flex items-center gap-2">
                    <StatusDot tone={tone} live={running && !held} />
                    <span className="text-ui font-semibold text-text">{title}</span>
                    {showStatus && <PlotterStatus className="ml-1" />}
                </div>
                <span className="numeric text-ui font-medium text-text">{percent.toFixed(0)}%</span>
            </div>

            <Progress value={percent} tone={job.phase === "failed" ? "danger" : held ? "warn" : "accent"} label="Plot progress" />

            <div className="flex items-center justify-between text-caption text-muted">
                <span className="numeric">
                    {running && progress
                        ? `Line ${progress.current_line.toLocaleString()} / ${progress.total_lines.toLocaleString()}`
                        : job.result
                            ? `Took ${formatDuration(job.result.duration_seconds)}`
                            : running
                                ? "Starting…"
                                : ""}
                </span>
                {running && progress && <span className="numeric">{formatDuration(progress.estimated_remaining_seconds)} left</span>}
            </div>

            {showPosition && telemetry && (
                <dl aria-label="Position" className="grid grid-cols-3 gap-1 rounded-control bg-surface-sunken px-3 py-2 text-caption">
                    {(["X", "Y", "Z"] as const).map((axis, index) => (
                        <div key={axis} className="flex gap-1.5">
                            <dt className="font-semibold text-subtle">{axis}</dt>
                            <dd className="numeric text-text">{telemetry.mpos[index].toFixed(2)}</dd>
                        </div>
                    ))}
                </dl>
            )}

            <div className="mt-0.5 flex gap-2">
                {running ? (
                    <>
                        <Button className="flex-1" onClick={() => void (held ? resume() : hold())}>
                            {held ? <Play weight="fill" /> : <Pause weight="fill" />}
                            {held ? "Resume" : "Pause"}
                        </Button>
                        <Button className="flex-1" variant={confirmAbort ? "danger-solid" : "danger"} onClick={onAbort} aria-live="polite">
                            <Stop weight="fill" />
                            {confirmAbort ? "Tap to abort" : "Abort"}
                        </Button>
                    </>
                ) : (
                    <Button className="flex-1" onClick={dismiss}>
                        <X />
                        Dismiss
                    </Button>
                )}
            </div>
        </section>
    );
}
