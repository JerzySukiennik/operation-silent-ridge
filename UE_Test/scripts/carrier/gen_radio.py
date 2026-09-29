"""LSO / pilot radio calls: Piper TTS (voices: en_US-norman-medium = public domain LibriVox data, trained from scratch; en_US-ljspeech-medium
= public domain LJ Speech) -> UHF radio filter (band-pass, soft clip, hiss, squelch tail) -> 44.1 kHz mono WAV for UE.
Needs a venv with piper-tts (uv venv -p 3.12; uv pip install piper-tts) and the .onnx voices in VOICES.
Usage: <venv>/bin/python gen_radio.py <voices_dir> <out_dir>"""
import sys, os, wave, subprocess, numpy as np
from piper import PiperVoice

VOICES, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
LINES = {
    "pilot_ball": ("ljspeech", "Lightning, ball, four point two."),
    "lso_roger_ball": ("norman", "Roger ball."),
    "lso_power": ("norman", "Power, power."),
    "lso_low": ("norman", "You're low."),
    "lso_high": ("norman", "You're high."),
    "lso_come_left": ("norman", "Come left."),
    "lso_right": ("norman", "Right for lineup."),
    "lso_slow": ("norman", "You're slow."),
    "lso_fast": ("norman", "You're fast."),
    "lso_easy": ("norman", "Easy with it."),
    "lso_waveoff": ("norman", "Wave off, wave off."),
    "lso_bolter": ("norman", "Bolter, bolter, bolter!"),
    "lso_hook": ("norman", "Hook down!"),
}
SR = 44100
rng = np.random.default_rng(3)


def bandpass(x, sr, lo=380.0, hi=2900.0):
    F = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / sr)
    g = 1 / np.sqrt(1 + (lo / np.maximum(f, 1)) ** 6) / np.sqrt(1 + (f / hi) ** 8)
    return np.fft.irfft(F * g, len(x))


def radio(x, sr):
    x = x / (np.max(np.abs(x)) + 1e-9)
    x = bandpass(x, sr)
    x = np.tanh(x * 3.2) / np.tanh(3.2)                # soft clipping (AM/UHF compression)
    hiss = bandpass(rng.normal(0, 1, len(x)), sr, 900, 5000) * 0.035
    y = x * 0.8 + hiss
    tail = bandpass(rng.normal(0, 1, int(0.09 * sr)), sr, 700, 6000) * 0.25 * np.linspace(1, 0, int(0.09 * sr)) ** 2
    head = bandpass(rng.normal(0, 1, int(0.03 * sr)), sr, 700, 6000) * 0.12
    y = np.concatenate([head, y, tail])
    return np.clip(y / (np.max(np.abs(y)) + 1e-9) * 0.85, -1, 1)


def write(path, y, sr):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((y * 32767).astype(np.int16).tobytes())


voices = {n: PiperVoice.load(os.path.join(VOICES, f"en_US-{n}-medium.onnx")) for n in ("norman", "ljspeech")}
for name, (v, text) in LINES.items():
    vc = voices[v]
    chunks = [np.frombuffer(c.audio_int16_bytes, np.int16).astype(np.float32) / 32768.0 for c in vc.synthesize(text)]
    x = np.concatenate(chunks)
    # trim leading/trailing silence and long internal pauses (Piper adds sentence gaps)
    env = np.convolve(np.abs(x), np.ones(441) / 441, "same")
    on = np.where(env > 0.02)[0]
    x = x[max(on[0] - 200, 0):on[-1] + 600]
    sr = vc.config.sample_rate
    # resample to 44.1 kHz (linear is fine inside a 3 kHz radio band)
    t = np.arange(int(len(x) * SR / sr)) * sr / SR
    x = np.interp(t, np.arange(len(x)), x)
    write(os.path.join(OUT, name + ".wav"), radio(x, SR), SR)
    print("wrote", name, round(len(x) / SR, 2), "s")
# squelch click / carrier burst
n = int(0.12 * SR)
c = bandpass(rng.normal(0, 1, n), SR, 600, 6000) * np.exp(-np.linspace(0, 9, n))
write(os.path.join(OUT, "radio_click.wav"), np.clip(c / np.max(np.abs(c)) * 0.7, -1, 1), SR)
print("wrote radio_click")
