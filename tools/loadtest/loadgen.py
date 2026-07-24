#!/usr/bin/env python3
"""Concurrent-load generator for the JP2 decode path.

Fires `concurrency` parallel clients at a running sipi, each requesting a
distinct native-resolution tile of one image (distinct region => distinct cache
key => a real decode per request, mirroring a IIIF viewer's tile burst). Reports
throughput and latency percentiles per concurrency level, discarding a warm-up
window. Stdlib only (no third-party deps in the dev shell).

Usage:
    loadgen.py BASE_URL IMAGE_ID TILE OUT CONCS DUR WARM

    BASE_URL  e.g. http://localhost:2048
    IMAGE_ID  IIIF id, e.g. knora/load_test.jpx
    TILE      tile edge in px (region grid step), e.g. 512
    OUT       IIIF size, e.g. `max` (native-res tile; never upscales)
    CONCS     comma-separated concurrency levels, e.g. 10,20,40
    DUR       measurement seconds per level
    WARM      warm-up seconds discarded before measuring
"""
import sys, json, time, threading, urllib.request, urllib.error, collections


def tiles(w, h, step):
    out, y = [], 0
    while y < h:
        th, x = min(step, h - y), 0
        while x < w:
            out.append(f"{x},{y},{min(step, w - x)},{th}")
            x += step
        y += step
    return out


def run(base, idp, regions, out, conc, dur, warm):
    url = lambda i: f"{base}/{idp}/{regions[i % len(regions)]}/{out}/0/default.jpg"
    lock, samples, counter, stop = threading.Lock(), [], [0], threading.Event()
    errkind = collections.Counter()

    def worker():
        while not stop.is_set():
            with lock:
                i = counter[0]; counter[0] += 1
            t0, kind = time.perf_counter(), None
            try:
                r = urllib.request.urlopen(url(i), timeout=120)
                ok = (r.status == 200 and len(r.read()) > 0)
                if not ok:
                    kind = f"http_{r.status}"
            except urllib.error.HTTPError as e:
                ok, kind = False, f"http_{e.code}"
            except Exception as e:
                ok, kind = False, type(e).__name__
            fin = time.perf_counter()
            with lock:
                samples.append((fin, fin - t0, ok))
                if kind:
                    errkind[kind] += 1

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(conc)]
    start = time.perf_counter()
    for t in threads:
        t.start()
    time.sleep(warm + dur)
    stop.set()
    for t in threads:
        t.join(timeout=125)

    lo, hi = start + warm, start + warm + dur
    win = [(dt, ok) for (fin, dt, ok) in samples if lo <= fin <= hi]
    oks = sorted(dt for dt, ok in win if ok)
    pct = lambda a, p: a[min(len(a) - 1, int(round(p / 100.0 * (len(a) - 1))))] if a else None
    ms = lambda v: round(v * 1000, 1) if v is not None else None
    return {
        "concurrency": conc, "tiles": len(regions),
        "requests_ok": len(oks), "errors": sum(1 for _, ok in win if not ok),
        "err_kinds": dict(errkind), "throughput_rps": round(len(oks) / dur, 2),
        "p50_ms": ms(pct(oks, 50)), "p90_ms": ms(pct(oks, 90)),
        "p99_ms": ms(pct(oks, 99)), "max_ms": ms(oks[-1]) if oks else None,
    }


def main():
    base, idp, tile, out, concs, dur, warm = sys.argv[1:8]
    info = json.load(urllib.request.urlopen(f"{base}/{idp}/info.json", timeout=30))
    regions = tiles(info["width"], info["height"], int(tile))
    print(f"image {info['width']}x{info['height']}, {len(regions)} distinct tiles")
    for c in (int(x) for x in concs.split(",")):
        print(json.dumps(run(base, idp, regions, out, c, float(dur), float(warm))))


if __name__ == "__main__":
    main()
