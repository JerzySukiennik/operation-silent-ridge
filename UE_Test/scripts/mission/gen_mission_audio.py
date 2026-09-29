# Mission audio (M3): synthesized weapon/RWR/explosion sounds (numpy, seeded) + Piper TTS radio/cockpit voices
# through a radio band-pass. Output: UE_Test/Niepotrzebne/m3/audio/*.wav (16-bit mono 44.1 kHz), imported by ue_21_mission.py.
import os, subprocess, wave, numpy as np
from scipy import signal
SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Niepotrzebne", "m3", "audio")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1688)

def save(name, x, peak=0.9):
    x = np.asarray(x, np.float64)
    x = x / max(np.abs(x).max(), 1e-9) * peak
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())
    print("wrote", name, "%.2f s" % (len(x) / SR))

def t(sec): return np.arange(int(sec * SR)) / SR
def lp(x, f, o=2): return signal.sosfilt(signal.butter(o, f, "low", fs=SR, output="sos"), x)
def hp(x, f, o=2): return signal.sosfilt(signal.butter(o, f, "high", fs=SR, output="sos"), x)
def bp(x, a, b, o=2): return signal.sosfilt(signal.butter(o, [a, b], "band", fs=SR, output="sos"), x)
def noise(n): return rng.standard_normal(n)

# ---- GAU-22/A: 3,300 rpm = 55 shots/s; 1 s loop with exactly 55 shots (seamless), each a sharp crack + chamber thump
def shot_train(rate, sec, crack_ms, body_hz, lpf):
    n = int(sec * SR); x = np.zeros(n)
    k = int(0.02 * SR); env = np.exp(-np.arange(k) / (crack_ms / 1000 * SR))
    for i in range(int(rate * sec)):
        s = int(i * SR / rate) + rng.integers(0, 40)
        seg = noise(k) * env + 0.8 * np.sin(2 * np.pi * body_hz * np.arange(k) / SR) * np.exp(-np.arange(k) / (0.006 * SR))
        e = min(n, s + k); x[s:e] += seg[:e - s]
    return lp(x, lpf, 2) + 0.25 * lp(noise(n), 300) * 0.3
g = shot_train(55, 1.0, 2.5, 95, 5200)
save("gun_loop", g)
save("gun_burst", np.concatenate([shot_train(30, 0.9, 3.5, 70, 2500), np.zeros(int(0.1 * SR))]) * np.r_[np.ones(int(0.8 * SR)), np.linspace(1, 0, int(0.2 * SR))])

# ---- explosions: broadband burst + long low rumble (brown noise), crackle tail
def boom(sec, low, crack, bright):
    n = int(sec * SR); x = noise(n)
    tt = np.arange(n) / SR
    attack = 1 - np.exp(-tt / 0.004)
    body = lp(x, bright, 2) * np.exp(-tt / (0.18 * sec)) * attack
    brown = np.cumsum(noise(n)); brown = hp(brown - brown.mean(), 18)
    rumble = lp(brown, low, 3) * np.exp(-tt / (0.35 * sec)) * attack
    crk = (rng.random(n) < 0.002) * noise(n) * np.exp(-tt / (0.5 * sec)) * crack
    return body * 0.6 + rumble / np.abs(rumble).max() * 1.0 + lp(crk, 4000)
save("expl_bomb", boom(4.0, 120, 0.6, 2500))
save("expl_far", lp(boom(3.0, 90, 0.2, 900), 1500))
save("secondary", boom(5.0, 100, 1.0, 1800))
save("bullet_impact", boom(0.35, 300, 0.1, 3500))

# ---- SAM launch: ignition crack + roaring motor (filtered noise, rising then steady), 3.5 s
n = int(3.5 * SR); tt = np.arange(n) / SR
roar = lp(noise(n), 1800, 2) * (1 - np.exp(-tt / 0.08)) * np.clip(1.2 - tt / 3.5, 0, 1)
roar += 0.6 * lp(np.cumsum(noise(n)) * 0.02, 200) * (1 - np.exp(-tt / 0.1)) * np.clip(1.2 - tt / 3.5, 0, 1)
roar[:int(0.03 * SR)] += noise(int(0.03 * SR)) * 2.0
save("sam_launch", roar)

# ---- countermeasures + release
def pop(sec, f, body):
    tt = t(sec); return (noise(len(tt)) * np.exp(-tt / 0.012) + body * np.sin(2 * np.pi * f * tt) * np.exp(-tt / 0.05))
save("flare_pop", hp(pop(0.4, 140, 0.8), 60))
save("chaff_pop", lp(pop(0.35, 90, 0.5), 3000) + 0.3 * lp(noise(int(0.35 * SR)), 6000) * np.exp(-t(0.35) / 0.1))
tt = t(0.6)
clunk = np.sin(2 * np.pi * 70 * tt) * np.exp(-tt / 0.08) + 0.5 * np.sin(2 * np.pi * 210 * tt) * np.exp(-tt / 0.05) + 0.3 * lp(noise(len(tt)), 2000) * np.exp(-tt / 0.02)
save("bomb_release", clunk)
save("ui_click", hp(noise(int(0.03 * SR)) * np.exp(-t(0.03) / 0.003), 1500), 0.5)

# ---- RWR tones (ALR-style): search chirp, lock (continuous, pulsed), launch warble; loops are exact 1 s
tt = t(0.25)
chirp = np.sin(2 * np.pi * (1500 * tt + 2000 * tt ** 2)) * ((tt % 0.06) < 0.04) * np.exp(-tt / 0.2)
save("rwr_search", chirp, 0.6)
tt = t(1.0)
save("rwr_lock", np.sin(2 * np.pi * 1000 * tt) * (0.75 + 0.25 * (np.sin(2 * np.pi * 10 * tt) > 0)), 0.55)
f = np.where((tt * 8) % 1 < 0.5, 900.0, 1400.0)
save("rwr_launch", np.sin(2 * np.pi * np.cumsum(f) / SR), 0.6)

# ---- voices: Piper TTS (same public-domain-data voices as the carrier radio: en_US-norman-medium = LibriVox, en_US-ljspeech-medium =
# LJ Speech) run in the Piper venv, then a UHF radio chain (WATCHTOWER) or a cockpit chain (warning voice)
PIPER_PY = os.environ.get("OSR_PIPER_PY", "/private/tmp/claude-501/osr_carrier/tts/bin/python")
PIPER_VOICES = os.environ.get("OSR_PIPER_VOICES", "/private/tmp/claude-501/osr_carrier/voices")
PIPER_CODE = """
import sys, wave, numpy as np
from piper import PiperVoice
v = PiperVoice.load(sys.argv[1]); text = sys.argv[2]
x = np.concatenate([np.frombuffer(c.audio_int16_bytes, np.int16) for c in v.synthesize(text)])
with wave.open(sys.argv[3], 'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(v.config.sample_rate); w.writeframes(x.tobytes())
"""
def say(name, text, voice, radio=True, rate=1.0):
    model = os.path.join(PIPER_VOICES, "en_US-%s-medium.onnx" % ("ljspeech" if voice == "Samantha" else "norman"))
    raw = os.path.join(OUT, name + "_raw.wav")
    subprocess.run([PIPER_PY, "-c", PIPER_CODE, model, text, raw], check=True)
    with wave.open(raw) as w:
        sr = w.getframerate(); x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float64) / 32768
    os.remove(raw)
    env = np.convolve(np.abs(x), np.ones(441) / 441, "same"); on = np.where(env > 0.02)[0]
    x = x[max(on[0] - 200, 0):on[-1] + 600]
    tt = np.arange(int(len(x) * SR / sr / rate)) * sr * rate / SR
    x = np.interp(tt, np.arange(len(x)), x)
    if radio:
        x = bp(x, 350, 3000, 3); x = np.tanh(x / max(np.abs(x).max(), 1e-9) * 2.2)
        hiss = bp(noise(len(x)), 800, 5000) * 0.05
        key = bp(noise(int(0.12 * SR)), 500, 4000) * 0.25
        x = np.concatenate([key, x + hiss, key * 0.8])
    else:
        x = bp(x, 250, 5000, 2)
    save(name, x, 0.85)
say("r_ingress", "Ridge one, Watchtower. Feet dry. Stay low in the canyon, the valley radars are up.", "Daniel")
say("r_spiked", "Ridge one, you're spiked. Get low!", "Daniel")
say("r_target_area", "Ridge one, target area. Pop up, lase the vent shaft, and drop.", "Daniel")
say("r_hit", "Good hit! Good hit! Secondaries! The plant is done. Get out of there!", "Daniel", rate=1.1)
say("r_miss", "Negative effect. The shaft is intact. Re-attack the vent.", "Daniel")
say("r_bandits", "Bandits! Two Su fifty-sevens scrambling from the north-east airfield!", "Daniel", rate=1.08)
say("r_feet_wet", "Ridge one, feet wet. Mother is west. Bring it home.", "Daniel")
say("r_down", "Ridge one is down! Ridge one is down!", "Daniel", rate=1.08)
say("r_success", "Ridge one is aboard. Target destroyed. Outstanding work.", "Daniel")
say("v_missile_launch", "Missile launch. Missile launch.", "Samantha", radio=False, rate=1.05)
say("v_missile", "Missile. Missile.", "Samantha", radio=False, rate=1.05)
