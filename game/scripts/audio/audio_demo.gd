# Audio test: throttle sweep idle->MIL->AB, 250 m/s remote flybys (100 m and 400 m abeam), a supersonic pass with boom, a crash; records Master to WAV and quits.
extends Node3D

const TAS_KT := 1.943844
const SEAM_FILES := ["engine_idle_loop", "engine_mil_loop", "engine_ab_loop", "engine_far_loop", "wind_low_loop", "wind_high_loop", "buffet_loop"]

var mode := "demo"
var shots := "user://"
var clock := 0.0
var rec: AudioEffectRecord
var cam: Camera3D
var local_jet: Node3D
var local_audio: JetAudio
var remote_jet: Node3D
var remote_audio: JetAudio
var probe_jet: Node3D
var probe_audio: JetAudio
var probe_pitch := {"a": 0.0, "na": 0, "r": 0.0, "nr": 0}
var super_jet: Node3D
var super_audio: JetAudio
var events: Array[String] = []
var peak := {}
var marks := {}
var seam_player: AudioStreamPlayer
var seam_index := -1
var seam_next := 0.0
var fired := {}
var pitch_log := {"approach": 0.0, "recede": 10.0, "tau_cpa": 0.0}


func _ready() -> void:
	var args: Dictionary = Engine.get_meta("boot_args", {}) if Engine.has_meta("boot_args") else {}
	mode = str(args.get("audio_mode", "demo"))
	shots = str(args.get("shots", "user://"))
	JetAudio.ensure_buses()
	rec = AudioEffectRecord.new()
	rec.format = AudioStreamWAV.FORMAT_16_BITS
	var master := AudioServer.get_bus_index("Master")
	AudioServer.add_bus_effect(master, rec, AudioServer.get_bus_effect_count(master))
	cam = Camera3D.new()
	add_child(cam)
	cam.make_current()
	rec.set_recording_active(true)
	_event("record_start")
	print("REPORT audio_demo mode=%s mix_rate=%d driver_latency=%.3f" % [mode, AudioServer.get_mix_rate(), AudioServer.get_output_latency()])
	if mode == "demo":
		local_jet = Node3D.new()
		local_jet.position = Vector3(0.0, -2.5, -20.0)   # camera 20 m behind, 2.5 m above
		add_child(local_jet)
		local_audio = JetAudio.new()
		local_jet.add_child(local_audio)
		local_audio.setup(true)


func _process(delta: float) -> void:
	clock += delta
	_track_peak()
	if mode == "seams":
		_process_seams()
	else:
		_process_demo(delta)


# ------------------------------------------------------------------ demo

func _process_demo(delta: float) -> void:
	var t := clock
	if local_audio and is_instance_valid(local_audio):
		var thr := 0.0
		var ias := 0.0
		var ab := 0.0
		var aoa := 3.0
		var g := 1.0
		var gear := false
		if t < 3.0:
			thr = 0.0
		elif t < 9.0:
			thr = (t - 3.0) / 6.0
			ias = lerpf(0.0, 300.0, (t - 3.0) / 6.0)
		elif t < 16.0:
			thr = 1.0
			ias = lerpf(300.0, 560.0, clampf((t - 9.0) / 7.0, 0.0, 1.0))
			if t >= 11.0:
				ab = minf(1.0, 0.15 + (t - 11.0) * 1.2)
			if t > 14.0 and t < 15.5:
				aoa = 26.0
				g = 7.0
		else:
			thr = lerpf(1.0, 0.3, clampf((t - 16.0) / 2.0, 0.0, 1.0))
			ias = 480.0
			gear = t > 17.5
		_once("idle_start", t >= 0.0)
		_once("spool_up_start", t >= 3.0)
		_once("mil_reached", t >= 9.0)
		_once("ab_on", t >= 11.0)
		_once("buffet_on", t >= 14.0)
		_once("ab_off", t >= 16.0)
		_once("gear_down", t >= 17.5)
		local_audio.update({"throttle": thr, "ab": ab > 0.0, "ab_level": ab, "ias_kt": ias, "aoa_deg": aoa, "g": g, "gear": gear, "alive": true}, delta)
		if t >= 19.0:
			_event("local_removed")
			local_jet.queue_free()
			local_audio = null
	# Remote subsonic flyby: 250 m/s along +X, 100 m abeam, CPA at t = 29.5 s.
	if t >= 19.5 and remote_jet == null and not fired.has("remote_done"):
		remote_jet = Node3D.new()
		add_child(remote_jet)
		remote_audio = JetAudio.new()
		remote_jet.add_child(remote_audio)
		remote_audio.setup(false)
		_event("remote_spawn x=-2500 z=-100 v=250")
	if remote_jet and is_instance_valid(remote_jet):
		var rt := t - 19.5
		remote_jet.position = Vector3(-2500.0 + 250.0 * rt, 0.0, -100.0)
		remote_audio.update({"throttle": 0.85, "ab": false, "mach": 250.0 / 343.0, "alive": true}, delta)
		var d: Dictionary = remote_audio.debug
		_once("remote_cpa_true", rt >= 10.0)
		if rt > 3.0 and rt < 9.0:
			pitch_log.approach = maxf(pitch_log.approach, float(d.get("pitch", 0.0)))
		if rt > 14.0 and rt < 19.0:
			pitch_log.recede = minf(pitch_log.recede, float(d.get("pitch", 10.0)))
		if absf(rt - 10.0) < 0.02:
			pitch_log.tau_cpa = float(d.get("tau", 0.0))
		if d.get("duck", 0.0) > 0.99:
			_once("flyby_oneshot_fired", true)
		if rt >= 20.5:
			_event("remote_removed")
			print("REPORT flyby pitch_approach_max=%.2f pitch_recede_min=%.2f expected_approach=%.2f expected_recede=%.2f (clamped 0.5..2.5) tau_at_true_cpa=%.3f s (expected 100/sqrt(c^2-v^2)=%.3f)" % [
				pitch_log.approach, pitch_log.recede, 1.0 / (1.0 - 250.0 / 343.0 * 0.999), 1.0 / (1.0 + 250.0 / 343.0 * 0.999), pitch_log.tau_cpa, 100.0 / sqrt(343.0 * 343.0 - 250.0 * 250.0)])
			remote_jet.queue_free()
			remote_jet = null
			fired["remote_done"] = true
			remote_audio = null
	# Doppler probe: 250 m/s pass 400 m abeam (too far for the flyby one-shot), true CPA at t = 46 s.
	if t >= 40.0 and probe_jet == null and not fired.has("probe_done"):
		probe_jet = Node3D.new()
		add_child(probe_jet)
		probe_audio = JetAudio.new()
		probe_jet.add_child(probe_audio)
		probe_audio.setup(false)
		_event("probe_spawn x=-1500 z=-400 v=250")
	if probe_jet and is_instance_valid(probe_jet):
		var pt := t - 40.0
		probe_jet.position = Vector3(-1500.0 + 250.0 * pt, 0.0, -400.0)
		probe_audio.update({"throttle": 0.0, "ab": false, "mach": 250.0 / 343.0, "alive": true}, delta)
		var heard_rel := pt - 6.0 - 400.0 / sqrt(343.0 * 343.0 - 250.0 * 250.0)
		_once("probe_cpa_heard", heard_rel >= 0.0)
		var pd: Dictionary = probe_audio.debug
		if heard_rel > -2.2 and heard_rel < -0.8:
			probe_pitch.a += float(pd.get("pitch", 1.0)); probe_pitch.na += 1
		if heard_rel > 0.8 and heard_rel < 2.2:
			probe_pitch.r += float(pd.get("pitch", 1.0)); probe_pitch.nr += 1
		if pt >= 11.5:
			print("REPORT probe mean_pitch_approach=%.3f mean_pitch_recede=%.3f ratio=%.3f (windows heard_cpa-2.2..-0.8 s and +0.8..+2.2 s)" % [
				probe_pitch.a / maxf(probe_pitch.na, 1), probe_pitch.r / maxf(probe_pitch.nr, 1), (probe_pitch.a / maxf(probe_pitch.na, 1)) / maxf(probe_pitch.r / maxf(probe_pitch.nr, 1), 0.001)])
			probe_jet.queue_free()
			probe_jet = null
			fired["probe_done"] = true
			_event("probe_removed")
	# Supersonic pass, Mach 1.4 at 600 m AGL, overhead at t = 57.2 s; boom when the cone reaches the listener.
	if t >= 52.0 and super_jet == null and not fired.has("super_done"):
		super_jet = Node3D.new()
		add_child(super_jet)
		super_audio = JetAudio.new()
		super_jet.add_child(super_audio)
		super_audio.setup(false)
		_event("super_spawn M1.4 alt600")
	if super_jet and is_instance_valid(super_jet):
		var st := t - 52.0
		super_jet.position = Vector3(-2500.0 + 480.0 * st, 600.0, 0.0)
		super_audio.update({"throttle": 1.0, "ab": true, "ab_level": 1.0, "mach": 1.4, "alive": true}, delta)
		if super_audio.debug.get("cone", false):
			_once("boom_cone_entered", true)
		if st >= 10.0:
			super_jet.queue_free()
			super_jet = null
			fired["super_done"] = true
			_event("super_removed")
	# Crash of a (local) jet 30 m in front.
	if t >= 63.0 and not fired.has("crash"):
		fired["crash"] = true
		var cj := Node3D.new()
		cj.position = Vector3(0.0, 0.0, -30.0)
		add_child(cj)
		var ca := JetAudio.new()
		cj.add_child(ca)
		ca.setup(true)
		ca.play_crash()
		_event("crash")
	if t >= 70.0:
		_finish()


# ------------------------------------------------------------------ seams

func _process_seams() -> void:
	if clock < seam_next:
		return
	seam_index += 1
	if seam_player:
		seam_player.queue_free()
	if seam_index >= SEAM_FILES.size():
		_finish()
		return
	var s := load("res://assets/audio/%s.ogg" % SEAM_FILES[seam_index]) as AudioStreamOggVorbis
	s.loop = true
	seam_player = AudioStreamPlayer.new()
	seam_player.stream = s
	seam_player.bus = "SFX"
	add_child(seam_player)
	seam_player.play()
	_event("seam_%s len=%.3f" % [SEAM_FILES[seam_index], s.get_length()])
	seam_next = clock + s.get_length() * 1.35 + 0.6


# ------------------------------------------------------------------ util

func _once(name: String, cond: bool) -> void:
	if cond and not fired.has(name):
		fired[name] = true
		_event(name)


func _event(name: String) -> void:
	var line := "%.3f %s" % [clock, name]
	events.append(line)
	print("REPORT event t=%s" % line)


func _track_peak() -> void:
	var m := AudioServer.get_bus_index("Master")
	var p := maxf(AudioServer.get_bus_peak_volume_left_db(m, 0), AudioServer.get_bus_peak_volume_right_db(m, 0))
	var phase := int(clock / 5.0)
	peak[phase] = maxf(float(peak.get(phase, -200.0)), p)


func _finish() -> void:
	set_process(false)
	rec.set_recording_active(false)
	var wav := rec.get_recording()
	var path := shots.path_join("audio_%s.wav" % mode)
	var err := ERR_UNAVAILABLE
	var secs := 0.0
	if wav:
		err = wav.save_to_wav(path)
		secs = wav.get_length()
	var pk: Array[String] = []
	for k in peak.keys():
		pk.append("%d-%ds:%.1f" % [k * 5, k * 5 + 5, peak[k]])
	print("REPORT master_peak_dbfs_per_5s %s" % " ".join(pk))
	print("REPORT recorded path=%s err=%d seconds=%.2f clock=%.2f" % [path, err, secs, clock])
	get_tree().quit()
