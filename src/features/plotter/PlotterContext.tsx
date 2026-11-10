"use client";

import {
    createContext,
    useCallback,
    useContext,
    useEffect,
    useMemo,
    useRef,
    useState,
    type PropsWithChildren,
} from "react";

import useCanvas from "../workspace/canvas/CanvasContext";
import { exportCanvasToSvg } from "../workspace/canvas/exportSvg";
import { formatDuration } from "./format";
import type { GatewaySnapshot, JobComplete, JobProgress, Telemetry } from "./gateway/types";

/** Browser ↔ app server event stream. */
export type LinkState = "connecting" | "online" | "offline";

export type JobPhase = "running" | "done" | "aborted" | "failed";

export interface PlotterJob {
    id: string;
    phase: JobPhase;
    totalLines?: number;
    result?: JobComplete;
    /** Progress when the job ended (for aborted or failed plots). */
    percent?: number;
}

export interface ActivityEntry {
    id: number;
    at: number;
    level: "info" | "warn" | "error";
    text: string;
}

interface PlotterContextValue {
    link: LinkState;
    /** Whether the app server is connected to the Pi gateway. */
    gatewayConnected: boolean;
    telemetry: Telemetry | null;
    progress: JobProgress | null;
    job: PlotterJob | null;
    /** A Send is in flight. */
    sending: boolean;
    error: string | null;
    /** Recent plotter events since the page opened, newest first. */
    activity: ActivityEntry[];
    /** Machine is running something or a job is active. */
    busy: boolean;
    held: boolean;
    /** Plots the current page. */
    send: () => Promise<void>;
    /** Plots an SVG document (e.g. an Auto plot). */
    sendSvg: (svg: string, name: string) => Promise<void>;
    /** Asks for a .gcode file and plots it. */
    pickFile: () => void;
    hold: () => Promise<void>;
    resume: () => Promise<void>;
    abort: () => Promise<void>;
    unlock: () => Promise<void>;
    home: () => Promise<void>;
    jog: (move: { x?: number; y?: number; z?: number; feed: number }) => Promise<void>;
    /** Hides a finished job and clears the error. */
    dismiss: () => void;
}

const PlotterContext = createContext<PlotterContextValue | null>(null);

const MIN_RETRY_MS = 1000;
const MAX_RETRY_MS = 15_000;
const TELEMETRY_RENDER_MS = 100;
const MAX_ACTIVITY = 50;

async function postJson(url: string, body?: BodyInit, contentType = "application/json") {
    const response = await fetch(url, { method: "POST", headers: { "Content-Type": contentType }, body: body ?? "{}" });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error ?? `Request failed (HTTP ${response.status}).`);
    return data;
}

const phaseFor = (status: JobComplete["status"]): JobPhase =>
    status === "success" ? "done" : status === "aborted" ? "aborted" : "failed";

/**
 * Live plotter state and controls. Subscribes to /api/plotter/events (the app server
 * relays the Pi gateway's WebSocket as SSE) and sends commands through /api/plotter/*.
 */
export function PlotterProvider({ children }: PropsWithChildren) {
    const { canvas, workspace } = useCanvas() ?? {};
    const [link, setLink] = useState<LinkState>("connecting");
    const [gatewayConnected, setGatewayConnected] = useState(false);
    const [telemetry, setTelemetry] = useState<Telemetry | null>(null);
    const [progress, setProgress] = useState<JobProgress | null>(null);
    const [job, setJob] = useState<PlotterJob | null>(null);
    const [sending, setSending] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [activity, setActivity] = useState<ActivityEntry[]>([]);
    const activityId = useRef(0);
    const jobIdRef = useRef<string | null>(null);
    const machineStateRef = useRef<string | null>(null);
    const lastPercent = useRef(0);

    const logEvent = useCallback((level: ActivityEntry["level"], text: string) => {
        const entry = { id: ++activityId.current, at: Date.now(), level, text };
        setActivity((current) => [entry, ...current].slice(0, MAX_ACTIVITY));
    }, []);

    // Telemetry arrives every 200 ms; render it at most every TELEMETRY_RENDER_MS.
    // (A timer, not requestAnimationFrame, so it keeps up when the page isn't painting.)
    const pendingTelemetry = useRef<Telemetry | null>(null);
    const telemetryTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

    const onComplete = useCallback((result: JobComplete) => {
        if (result.status === "success") logEvent("info", `Plot finished in ${formatDuration(result.duration_seconds)}`);
        else if (result.status === "aborted") logEvent("warn", "Plot aborted");
        else logEvent("error", `Plot failed (${result.status})`);
        jobIdRef.current = null;
        const percent = result.status === "success" ? 100 : lastPercent.current;
        setProgress(null);
        setJob((current) =>
            current && current.id !== result.job_id ? current : { id: result.job_id, phase: phaseFor(result.status), result, percent },
        );
    }, [logEvent]);

    const onProgress = useCallback((next: JobProgress) => {
        if (jobIdRef.current !== next.job_id) {
            jobIdRef.current = next.job_id;
            logEvent("info", `Plotting ${next.job_id} (${next.total_lines.toLocaleString()} lines)`);
        }
        lastPercent.current = next.percent;
        setProgress(next);
        // Also picks up jobs started elsewhere (Auto mode, another device).
        setJob((current) =>
            current?.id === next.job_id && current.phase === "running"
                ? current
                : { id: next.job_id, phase: "running", totalLines: next.total_lines },
        );
    }, [logEvent]);

    useEffect(() => {
        let source: EventSource | null = null;
        let retryTimer: ReturnType<typeof setTimeout> | undefined;
        let attempts = 0;
        let disposed = false;
        let wasOnline = false;
        let gatewayUp: boolean | null = null;

        const setGateway = (connected: boolean) => {
            if (gatewayUp === connected) return;
            if (connected) logEvent("info", "Plotter connected");
            else if (gatewayUp !== null || wasOnline) logEvent("warn", "Plotter disconnected");
            gatewayUp = connected;
            setGatewayConnected(connected);
        };

        const flushTelemetry = () => {
            telemetryTimer.current = null;
            const next = pendingTelemetry.current;
            const previous = machineStateRef.current;
            if (next && previous && next.state !== previous) {
                if (next.state.startsWith("Hold") && !previous.startsWith("Hold")) logEvent("info", "Paused");
                else if (previous.startsWith("Hold") && next.state === "Run") logEvent("info", "Resumed");
                if (next.state === "Alarm") logEvent("error", "Machine alarm");
            }
            machineStateRef.current = next?.state ?? null;
            setTelemetry(next);
        };

        const open = () => {
            setLink("connecting");
            source = new EventSource("/api/plotter/events");

            source.onopen = () => {
                attempts = 0;
                if (!wasOnline && activityId.current > 0) logEvent("info", "Reconnected to the app server");
                wasOnline = true;
                setLink("online");
            };

            source.addEventListener("snapshot", (event) => {
                const snapshot: GatewaySnapshot = JSON.parse((event as MessageEvent).data);
                setGateway(snapshot.connected);
                machineStateRef.current = snapshot.telemetry?.state ?? null;
                setTelemetry(snapshot.telemetry);
                if (snapshot.progress) onProgress(snapshot.progress);
                else setProgress(null);
            });

            source.addEventListener("link", (event) => {
                const { connected } = JSON.parse((event as MessageEvent).data);
                setGateway(connected);
                if (!connected) {
                    machineStateRef.current = null;
                    setTelemetry(null);
                }
            });

            source.addEventListener("telemetry", (event) => {
                pendingTelemetry.current = JSON.parse((event as MessageEvent).data);
                telemetryTimer.current ??= setTimeout(flushTelemetry, TELEMETRY_RENDER_MS);
            });

            source.addEventListener("job_progress", (event) => onProgress(JSON.parse((event as MessageEvent).data)));
            source.addEventListener("job_complete", (event) => onComplete(JSON.parse((event as MessageEvent).data)));

            source.onerror = () => {
                if (wasOnline) logEvent("warn", "Lost connection to the app server");
                wasOnline = false;
                setLink("offline");
                // The browser retries on its own unless the stream is closed for good.
                if (source?.readyState !== EventSource.CLOSED || disposed) return;
                const delay = Math.min(MAX_RETRY_MS, MIN_RETRY_MS * 2 ** attempts++);
                retryTimer = setTimeout(open, delay);
            };
        };

        open();

        return () => {
            disposed = true;
            clearTimeout(retryTimer);
            source?.close();
            if (telemetryTimer.current !== null) clearTimeout(telemetryTimer.current);
        };
    }, [onComplete, onProgress, logEvent]);

    const startJob = useCallback(async (body: BodyInit, contentType: string, name: string) => {
        setSending(true);
        setError(null);
        try {
            const result = await postJson(`/api/plotter/jobs?start=1&name=${encodeURIComponent(name)}`, body, contentType);
            jobIdRef.current = result.jobId;
            lastPercent.current = 0;
            logEvent("info", `Sent “${name}” (${Number(result.linesCount).toLocaleString()} lines)`);
            setJob({ id: result.jobId, phase: "running", totalLines: result.linesCount });
        } catch (err) {
            const message = err instanceof Error ? err.message : "Couldn't send to the plotter.";
            logEvent("error", message);
            setError(message);
        } finally {
            setSending(false);
        }
    }, [logEvent]);

    const sendSvg = useCallback((svg: string, name: string) => startJob(svg, "image/svg+xml", name), [startJob]);

    const send = useCallback(async () => {
        if (!canvas || !workspace) return;
        const page = workspace.getWorkspace();
        if (!canvas.getObjects().some((object) => object !== page)) {
            setError("Nothing to plot: the page is empty.");
            return;
        }
        await sendSvg(exportCanvasToSvg(canvas, workspace), "Canvas");
    }, [canvas, workspace, sendSvg]);

    const pickFile = useCallback(() => {
        const input = document.createElement("input");
        input.type = "file";
        input.accept = ".gcode,.gc,.nc,.ngc,.txt";
        input.onchange = async () => {
            const file = input.files?.[0];
            if (file) await startJob(await file.text(), "text/plain", file.name);
        };
        input.click();
    }, [startJob]);

    const control = useCallback(async (action: "hold" | "resume" | "abort" | "unlock" | "home") => {
        setError(null);
        try {
            await postJson(`/api/plotter/control/${action}`);
            if (action === "abort" && jobIdRef.current) {
                // The spec doesn't promise a job_complete after an abort.
                logEvent("warn", "Plot aborted");
                jobIdRef.current = null;
                setProgress(null);
                setJob((current) =>
                    current?.phase === "running" ? { ...current, phase: "aborted", percent: lastPercent.current } : current,
                );
            } else if (action === "unlock") {
                logEvent("info", "Alarm unlocked ($X)");
            } else if (action === "home") {
                logEvent("info", "Homing triggered ($H)");
            }
        } catch (err) {
            const message = err instanceof Error ? err.message : `Couldn't ${action}.`;
            logEvent("error", message);
            setError(message);
        }
    }, [logEvent]);

    const jog = useCallback(async (move: { x?: number; y?: number; z?: number; feed: number }) => {
        try {
            await postJson("/api/plotter/control/jog", JSON.stringify(move));
            const parts = (["x", "y", "z"] as const)
                .filter((axis) => move[axis])
                .map((axis) => `${axis.toUpperCase()}${move[axis]! > 0 ? "+" : ""}${move[axis]}`);
            logEvent("info", `Jog ${parts.join(" ")} mm`);
        } catch (err) {
            const message = err instanceof Error ? err.message : "Couldn't jog.";
            logEvent("error", message);
            setError(message);
        }
    }, [logEvent]);

    const dismiss = useCallback(() => {
        setError(null);
        setJob((current) => (current && current.phase !== "running" ? null : current));
    }, []);

    const held = telemetry?.state.startsWith("Hold") ?? false;
    const busy = job?.phase === "running" || (telemetry !== null && telemetry.state !== "Idle");

    const value = useMemo<PlotterContextValue>(
        () => ({
            link,
            gatewayConnected,
            telemetry,
            progress,
            job,
            sending,
            error,
            activity,
            busy,
            held,
            send,
            sendSvg,
            pickFile,
            hold: () => control("hold"),
            resume: () => control("resume"),
            abort: () => control("abort"),
            unlock: () => control("unlock"),
            home: () => control("home"),
            jog,
            dismiss,
        }),
        [link, gatewayConnected, telemetry, progress, job, sending, error, activity, busy, held, send, sendSvg, pickFile, control, jog, dismiss],
    );

    return <PlotterContext.Provider value={value}>{children}</PlotterContext.Provider>;
}

export function usePlotter() {
    const context = useContext(PlotterContext);
    if (!context) throw new Error("usePlotter must be used within PlotterProvider");
    return context;
}
