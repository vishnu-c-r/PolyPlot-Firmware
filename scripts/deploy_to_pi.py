#!/usr/bin/env python3
"""
Deploy updated water-plot pipeline and settings to Raspberry Pi (192.168.33.172)
"""
import os
import sys
import tarfile
import time
import paramiko

PI_HOST = "192.168.33.172"
PI_USER = "rpi3b"
PI_PASS = "pi3b"

def deploy():
    print(f"=== DEPLOYING TO PI ({PI_HOST}) ===", flush=True)
    
    # 1. Connect SSH
    print("[1/6] Connecting to Raspberry Pi via SSH/SFTP...", flush=True)
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(PI_HOST, username=PI_USER, password=PI_PASS, timeout=10)
    sftp = client.open_sftp()
    print("      Connected successfully.", flush=True)

    # 2. Package water-plot updates
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src_dir = os.path.join(base_dir, "src")
    pkg_json = os.path.join(base_dir, "package.json")
    next_dir = os.path.join(base_dir, ".next")
    dist_dir = os.path.join(base_dir, "dist")
    worker_cjs = os.path.join(dist_dir, "worker.cjs")

    print("[2/6] Building worker bundle with esbuild...", flush=True)
    import subprocess
    subprocess.run(
        ["npx", "esbuild", "worker/index.ts", "--bundle", "--platform=node", "--target=node20", "--outfile=dist/worker.cjs", "--external:better-sqlite3"],
        cwd=base_dir,
        shell=True,
        check=True
    )

    tar_path = os.path.join(base_dir, "water-plot-deploy.tar.gz")
    print(f"      Packaging src/, dist/, and .next/ into {tar_path}...", flush=True)
    def filter_func(tarinfo):
        norm = tarinfo.name.replace("\\", "/")
        parts = norm.split("/")
        if "cache" in parts:
            return None
        return tarinfo

    with tarfile.open(tar_path, "w:gz") as tar:
        tar.add(src_dir, arcname="src")
        tar.add(pkg_json, arcname="package.json")
        tar.add(dist_dir, arcname="dist")
        if os.path.exists(next_dir):
            tar.add(next_dir, arcname=".next", filter=filter_func)
    size_mb = os.path.getsize(tar_path) / (1024 * 1024)
    print(f"      Archive created ({size_mb:.2f} MB).", flush=True)

    # 3. Upload to Pi
    print(f"[3/6] Uploading {tar_path} to /tmp/deploy_update.tar.gz...", flush=True)
    sftp.put(tar_path, "/tmp/deploy_update.tar.gz")
    sftp.close()
    if os.path.exists(tar_path):
        os.remove(tar_path)
    print("      Upload complete.", flush=True)

    # 4. Extract and restart services
    print("[4/6] Extracting files and restarting systemd services...", flush=True)
    cmd = """
    echo pi3b | sudo -S tar -xzf /tmp/deploy_update.tar.gz -C /opt/spirit/water-plot
    rm -f /tmp/deploy_update.tar.gz
    echo pi3b | sudo -S systemctl restart plotter-studio.service
    """
    stdin, stdout, stderr = client.exec_command(cmd, timeout=120)
    out = stdout.read().decode().strip()
    err = stderr.read().decode().strip()
    if out:
        print("      Output:", out, flush=True)
    if err and "password" not in err.lower():
        print("      Stderr:", err, flush=True)

    # 5. Wait for services to stabilize
    print("[5/6] Waiting 6 seconds for Next.js, Worker, and Gateway to stabilize...", flush=True)
    time.sleep(6)

    # 6. Check service status
    print("[6/6] Verifying systemd service health...", flush=True)
    stdin, stdout, stderr = client.exec_command("systemctl is-active spirit.service plotter-studio.service plotter-worker.service")
    status = stdout.read().decode().strip().split()
    print(f"      spirit.service: {status[0] if len(status) > 0 else 'unknown'}", flush=True)
    print(f"      plotter-studio.service: {status[1] if len(status) > 1 else 'unknown'}", flush=True)
    print(f"      plotter-worker.service: {status[2] if len(status) > 2 else 'unknown'}", flush=True)

    client.close()
    print("=== DEPLOYMENT COMPLETE ===", flush=True)

if __name__ == "__main__":
    deploy()
