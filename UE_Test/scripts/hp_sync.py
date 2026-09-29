#!/usr/bin/env python3
# Incremental Mac -> HP project sync over SSH/SCP (no SMB, no firewall changes). Keeps an MD5 manifest of what the HP's
# project copy (C:\Users\jurek\osr_ue\proj\SilentRidgeUE) contains; each run tars only new/changed files, uploads them,
# deletes removed ones and stores the new manifest. Prints a JSON summary on the last stdout line:
#   {"changed": n, "deleted": n, "bytes": n, "kinds": ["content", "config", "source"], "seconds": s}
# Usage: hp_sync.py [--rehash-remote] [--dry-run]
#   --rehash-remote  rebuild the HP manifest by hashing the files actually on the HP (first run / after manual edits there)
import hashlib, json, os, subprocess, sys, tarfile, tempfile, time

T0 = time.time()
UET = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJ = os.path.join(UET, "SilentRidgeUE")
WORK = os.path.join(UET, "Niepotrzebne", "fastiter"); os.makedirs(WORK, exist_ok=True)
H = "jurek@MACNOTBOOK.local"
RP = r"C:\Users\jurek\osr_ue\proj\SilentRidgeUE"
RMAN = r"C:\Users\jurek\osr_ue\sync_manifest.txt"
DIRS = ["Content", "Config", "Source"]
SSH = ["ssh", "-o", "ConnectTimeout=20", "-o", "ServerAliveInterval=15", H]

# the HP builds the Game target only (no .NET Framework SDK for editor targets) and gets extra shader-compile threads
UPROJECT = """{
	"FileVersion": 3,
	"EngineAssociation": "5.7",
	"Category": "",
	"Description": "Operation Silent Ridge (UE 5.7 port)",
	"Modules": [
		{ "Name": "SilentRidgeUE", "Type": "Runtime", "LoadingPhase": "Default", "TargetAllowList": [ "Game" ] }
	]
}
""".encode()
ENGINE_EXTRA = b"\n[DevOptions.Shaders]\nNumUnusedShaderCompilingThreads=6\n"


def local_files():
    """rel path (forward slashes) -> (source path or bytes)"""
    out = {}
    for d in DIRS:
        for root, dirs, files in os.walk(os.path.join(PROJ, d)):
            dirs[:] = [x for x in dirs if not x.startswith(".")]
            for f in files:
                if f.startswith(".") or f.endswith((".pyc",)):
                    continue
                p = os.path.join(root, f)
                out[os.path.relpath(p, PROJ).replace(os.sep, "/")] = p
    out["SilentRidgeUE.uproject"] = UPROJECT
    out["Config/DefaultEngine.ini"] = open(os.path.join(PROJ, "Config", "DefaultEngine.ini"), "rb").read() + ENGINE_EXTRA
    return out


def local_hashes(files):
    cache_p = os.path.join(WORK, "hashcache.json")
    try:
        cache = json.load(open(cache_p))
    except Exception:
        cache = {}
    out = {}
    for rel, src in files.items():
        if isinstance(src, bytes):
            out[rel] = hashlib.md5(src).hexdigest(); continue
        st = os.stat(src)
        key = [st.st_size, st.st_mtime_ns]
        c = cache.get(rel)
        if c and c[:2] == key:
            out[rel] = c[2]; continue
        h = hashlib.md5()
        with open(src, "rb") as f:
            for chunk in iter(lambda: f.read(8 << 20), b""):
                h.update(chunk)
        out[rel] = h.hexdigest()
        cache[rel] = key + [out[rel]]
    json.dump(cache, open(cache_p, "w"))
    return out


def remote_manifest(rehash):
    if rehash:
        ps = ("$b='%s'; foreach ($d in 'Content','Config','Source') { Get-ChildItem -Recurse -File (Join-Path $b $d) -ErrorAction SilentlyContinue | "
              "ForEach-Object { (Get-FileHash -Algorithm MD5 $_.FullName).Hash.ToLower() + [char]9 + $_.FullName.Substring($b.Length+1).Replace('\\','/') } }; "
              "if (Test-Path (Join-Path $b 'SilentRidgeUE.uproject')) { (Get-FileHash -Algorithm MD5 (Join-Path $b 'SilentRidgeUE.uproject')).Hash.ToLower() + [char]9 + 'SilentRidgeUE.uproject' }").replace("%s", RP, 1)
        txt = subprocess.run(SSH + [ps], capture_output=True, text=True, check=True).stdout
    else:
        r = subprocess.run(SSH + ["if (Test-Path '%s') { Get-Content -Raw '%s' }" % (RMAN, RMAN)], capture_output=True, text=True, check=True)
        txt = r.stdout
        if not txt.strip():
            return remote_manifest(True)
    m = {}
    for line in txt.splitlines():
        if "\t" in line:
            h, rel = line.strip().split("\t", 1)
            m[rel] = h
    return m


def main():
    rehash = "--rehash-remote" in sys.argv
    dry = "--dry-run" in sys.argv
    files = local_files()
    lh = local_hashes(files)
    rh = remote_manifest(rehash)
    changed = sorted(r for r, h in lh.items() if rh.get(r) != h)
    deleted = sorted(r for r in rh if r not in lh)
    kinds = sorted({("content" if r.startswith("Content/") else "config" if r.startswith("Config/") else "source" if r.startswith("Source/") else "project")
                    for r in changed + deleted})
    nbytes = sum(len(files[r]) if isinstance(files[r], bytes) else os.path.getsize(files[r]) for r in changed)
    print("sync: %d changed (%.1f MB), %d deleted, kinds %s" % (len(changed), nbytes / 1e6, len(deleted), kinds), flush=True)
    for r in changed[:12]:
        print("  +", r)
    for r in deleted[:12]:
        print("  -", r)
    if not dry and (changed or deleted):
        if changed:
            tmp = tempfile.mkdtemp(prefix="osrsync")
            tp = os.path.join(tmp, "delta.tar")
            with tarfile.open(tp, "w", format=tarfile.PAX_FORMAT) as t:
                for r in changed:
                    src = files[r]
                    if isinstance(src, bytes):
                        ti = tarfile.TarInfo(r); ti.size = len(src); ti.mtime = int(time.time())
                        import io
                        t.addfile(ti, io.BytesIO(src))
                    else:
                        t.add(src, arcname=r)
            for attempt in range(5):
                if subprocess.run(["scp", "-q", "-o", "ServerAliveInterval=15", "-o", "ConnectTimeout=20", tp, H + ":C:/Users/jurek/osr_ue/delta.tar"]).returncode == 0:
                    break
                print("scp retry", attempt + 1, flush=True); time.sleep(5)
            else:
                sys.exit("scp failed")
            os.remove(tp)
        ps = []
        if deleted:
            lst = os.path.join(WORK, "deleted.txt")
            open(lst, "w").write("\n".join(r.replace("/", "\\") for r in deleted))
            subprocess.run(["scp", "-q", lst, H + ":C:/Users/jurek/osr_ue/deleted.txt"], check=True)
            ps.append(r"Get-Content C:\Users\jurek\osr_ue\deleted.txt | ForEach-Object { Remove-Item -Force (Join-Path '%s' $_) -ErrorAction SilentlyContinue }" % RP)
        if changed:
            ps.append(r"tar -xf C:\Users\jurek\osr_ue\delta.tar -C '%s'; if ($LASTEXITCODE -ne 0) { 'TAR FAILED'; exit 1 }; Remove-Item C:\Users\jurek\osr_ue\delta.tar" % RP)
        r = subprocess.run(SSH + ["; ".join(ps)], capture_output=True, text=True)
        if r.returncode != 0 or "FAILED" in r.stdout:
            sys.exit("remote apply failed: " + r.stdout + r.stderr)
    if not dry:
        man = os.path.join(WORK, "sync_manifest.txt")
        open(man, "w", newline="\n").write("".join("%s\t%s\n" % (h, r) for r, h in sorted(lh.items())))
        subprocess.run(["scp", "-q", man, H + ":C:/Users/jurek/osr_ue/sync_manifest.txt"], check=True)
    print(json.dumps({"changed": len(changed), "deleted": len(deleted), "bytes": nbytes, "kinds": kinds, "seconds": round(time.time() - T0, 1)}))


if __name__ == "__main__":
    main()
