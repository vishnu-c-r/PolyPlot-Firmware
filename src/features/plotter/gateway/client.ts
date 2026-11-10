import type { GatewayStatus, JogRequest, UploadResult } from "./types";

const REQUEST_TIMEOUT_MS = 5000;
/** Uploads can be several MB over a Pi's WiFi. */
const UPLOAD_TIMEOUT_MS = 30_000;

/**
 * A failed gateway call. `status` is what our API should answer with:
 * 503 not configured or unreachable, 504 timed out, otherwise the gateway's own status
 * (or 502 when it answered with something unusable).
 */
export class GatewayError extends Error {
    constructor(
        message: string,
        readonly status: number,
    ) {
        super(message);
        this.name = "GatewayError";
    }
}

/** Base URL of the Pi gateway, e.g. http://plotter-pi.local:8080 (from GATEWAY_URL). */
export function gatewayUrl(): string {
    const url = process.env.GATEWAY_URL?.trim();
    if (!url) throw new GatewayError("Plotter not configured: set GATEWAY_URL.", 503);
    return url.replace(/\/+$/, "");
}

/** The gateway's telemetry WebSocket URL. */
export function gatewaySocketUrl(): string {
    const custom = process.env.GATEWAY_WS_URL?.trim();
    if (custom) return custom;
    const base = gatewayUrl();
    try {
        const u = new URL(base);
        if (u.port === "8080") {
            return `ws://${u.hostname}:8081`;
        }
    } catch { }
    return `${base.replace(/^http/, "ws")}/ws`;
}

async function request<T>(path: string, init: RequestInit = {}, timeout = REQUEST_TIMEOUT_MS): Promise<T> {
    const url = `${gatewayUrl()}${path}`;
    let response: Response;

    try {
        response = await fetch(url, {
            ...init,
            headers: { "Content-Type": "application/json", ...init.headers },
            cache: "no-store",
            signal: AbortSignal.timeout(timeout),
        });
    } catch (error) {
        if (error instanceof DOMException && error.name === "TimeoutError") {
            throw new GatewayError("The plotter didn't respond in time.", 504);
        }
        throw new GatewayError("Can't reach the plotter.", 503);
    }

    const body = await response.json().catch(() => null);

    if (!response.ok) {
        const message = (body && (body.error ?? body.detail ?? body.message)) || `Plotter error (HTTP ${response.status}).`;
        throw new GatewayError(String(message), response.status >= 500 ? 502 : response.status);
    }
    if (body === null) throw new GatewayError("The plotter sent an invalid response.", 502);

    return body as T;
}

const post = <T>(path: string, body: unknown = {}, timeout?: number) =>
    request<T>(path, { method: "POST", body: JSON.stringify(body) }, timeout);

/** Uploads G-code; the gateway validates it and queues the job. */
export const uploadJob = (name: string, gcode: string) =>
    post<UploadResult>("/api/jobs/upload", { name, gcode }, UPLOAD_TIMEOUT_MS);

export const startJob = (jobId: string) => post<{ status: string }>("/api/jobs/start", { job_id: jobId });

/** Feed hold (`!`). */
export const hold = () => post<{ status: string }>("/api/control/hold");

/** Cycle resume (`~`). */
export const resume = () => post<{ status: string }>("/api/control/resume");

/** Abort and reset (Ctrl-X). */
export const abort = () => post<{ status: string }>("/api/control/abort");

/** Clear alarm / unlock ($X). */
export const unlock = () => post<{ status: string }>("/api/control/unlock");

/** Execute homing cycle ($H). */
export const home = () => post<{ status: string }>("/api/control/home");

export const jog = (move: JogRequest) => post<{ status: string }>("/api/control/jog", move);

export const getStatus = () => request<GatewayStatus>("/api/status");
