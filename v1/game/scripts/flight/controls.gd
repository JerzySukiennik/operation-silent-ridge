# Controls autoload: registers gamepad-first input actions in code and publishes one shaped ControlInput per physics tick (or the autotest override).
extends Node

const STICK_DEADZONE := 0.07
const PITCH_EXPO := 0.55          # 0 linear .. 1 cubic: fine control near centre, full authority at the edge
const ROLL_EXPO := 0.45
const LOOK_DEADZONE := 0.15
const LEVER_RATE := 0.6           # lever travel per second at full trigger (idle -> MIL in ~1.7 s)
const AB_RATE := 1.6              # afterburner range per second
const DETENT_HOLD := 0.25         # s of full trigger at MIL (after a release) to push through into afterburner
const RUDDER_RAMP := 4.0          # pedal travel per second from bumpers

var input := ControlInput.new()   # what the aircraft reads each physics tick
var override: ControlInput = null # set by autotest; replaces the pilot entirely while non-null
var invert_pitch := false
var enabled := true               # false while menus own the pad

var _lever := 0.8                 # 0..1 dry, then afterburner 1..2
var _detent_timer := 0.0
var _detent_armed := false
var _rudder := 0.0
var _gear_prev := false
var _help_prev := false

const ACTIONS := {
	"osr_pitch_up": [["axis", JOY_AXIS_LEFT_Y, 1.0], ["key", KEY_S], ["key", KEY_DOWN]],
	"osr_pitch_down": [["axis", JOY_AXIS_LEFT_Y, -1.0], ["key", KEY_W], ["key", KEY_UP]],
	"osr_roll_left": [["axis", JOY_AXIS_LEFT_X, -1.0], ["key", KEY_A], ["key", KEY_LEFT]],
	"osr_roll_right": [["axis", JOY_AXIS_LEFT_X, 1.0], ["key", KEY_D], ["key", KEY_RIGHT]],
	"osr_yaw_left": [["button", JOY_BUTTON_LEFT_SHOULDER], ["key", KEY_Q]],
	"osr_yaw_right": [["button", JOY_BUTTON_RIGHT_SHOULDER], ["key", KEY_E]],
	"osr_throttle_up": [["axis", JOY_AXIS_TRIGGER_RIGHT, 1.0], ["key", KEY_SHIFT]],
	"osr_throttle_down": [["axis", JOY_AXIS_TRIGGER_LEFT, 1.0], ["key", KEY_CTRL]],
	"osr_brake": [["button", JOY_BUTTON_B], ["key", KEY_B]],
	"osr_gear": [["button", JOY_BUTTON_Y], ["key", KEY_G]],
	"osr_look_left": [["axis", JOY_AXIS_RIGHT_X, -1.0], ["key", KEY_J]],
	"osr_look_right": [["axis", JOY_AXIS_RIGHT_X, 1.0], ["key", KEY_L]],
	"osr_look_up": [["axis", JOY_AXIS_RIGHT_Y, -1.0], ["key", KEY_I]],
	"osr_look_down": [["axis", JOY_AXIS_RIGHT_Y, 1.0], ["key", KEY_K]],
	"osr_look_back": [["button", JOY_BUTTON_RIGHT_STICK], ["key", KEY_C]],
	"osr_help": [["button", JOY_BUTTON_BACK], ["key", KEY_F1]],
}

const LAYOUT_HELP := [
	["Left stick", "Pitch (G command) / roll (roll rate)"],
	["RT / LT", "Throttle up / down; at MIL release RT, then press and hold for AB"],
	["LB / RB", "Rudder left / right"],
	["Right stick", "Look around (auto-recentres)"],
	["R3 (hold)", "Look back"],
	["B (hold)", "Speedbrake"],
	["Y", "Landing gear"],
	["Back / View", "This help"],
]


func _ready() -> void:
	process_physics_priority = -100
	register_actions()


static func register_actions() -> void:
	for action: String in ACTIONS:
		if InputMap.has_action(action):
			continue
		InputMap.add_action(action, 0.0)
		for spec: Array in ACTIONS[action]:
			var ev: InputEvent
			match spec[0]:
				"axis":
					var m := InputEventJoypadMotion.new()
					m.axis = spec[1]
					m.axis_value = spec[2]
					m.device = -1
					ev = m
				"button":
					var b := InputEventJoypadButton.new()
					b.button_index = spec[1]
					b.device = -1
					ev = b
				"key":
					var k := InputEventKey.new()
					k.physical_keycode = spec[1]
					ev = k
			InputMap.action_add_event(action, ev)


static func shape_stick(v: Vector2, deadzone: float) -> Vector2:
	var l := v.length()
	if l <= deadzone:
		return Vector2.ZERO
	var scaled := minf((l - deadzone) / (1.0 - deadzone), 1.0)
	return v / l * scaled


static func expo(x: float, k: float) -> float:
	return (1.0 - k) * x + k * x * x * x


func _physics_process(delta: float) -> void:
	if override != null:
		input.copy_from(override)
		override.gear_toggle = false
		override.help_toggle = false
		return
	if not enabled:
		input.pitch = 0.0
		input.roll = 0.0
		input.yaw = 0.0
		input.look = Vector2.ZERO
		return
	var s := shape_stick(Vector2(
		_raw("osr_roll_right") - _raw("osr_roll_left"),
		_raw("osr_pitch_up") - _raw("osr_pitch_down")), STICK_DEADZONE)
	input.roll = expo(s.x, ROLL_EXPO)
	input.pitch = expo(s.y, PITCH_EXPO) * (-1.0 if invert_pitch else 1.0)

	var rudder_target := _raw("osr_yaw_right") - _raw("osr_yaw_left")
	_rudder = move_toward(_rudder, rudder_target, RUDDER_RAMP * delta)
	input.yaw = _rudder

	_update_lever(delta, _raw("osr_throttle_up"), _raw("osr_throttle_down"))
	input.throttle = minf(_lever, 1.0)
	input.afterburner = clampf(_lever - 1.0, 0.0, 1.0) if _lever > 1.0 else 0.0
	if _lever > 1.0 and input.afterburner < 0.02:
		input.afterburner = 0.02
	input.brake = Input.is_action_pressed("osr_brake")

	var gear := Input.is_action_pressed("osr_gear")
	input.gear_toggle = gear and not _gear_prev
	_gear_prev = gear
	var help := Input.is_action_pressed("osr_help")
	input.help_toggle = help and not _help_prev
	_help_prev = help

	input.look = shape_stick(Vector2(
		_raw("osr_look_right") - _raw("osr_look_left"),
		_raw("osr_look_up") - _raw("osr_look_down")), LOOK_DEADZONE)
	input.look_back = Input.is_action_pressed("osr_look_back")


func _update_lever(delta: float, up: float, down: float) -> void:
	var up_s := smoothstep(0.05, 1.0, up)
	var down_s := smoothstep(0.05, 1.0, down)
	if down_s > 0.0:
		_detent_timer = 0.0
		if _lever > 1.0:
			_lever = maxf(1.0, _lever - AB_RATE * down_s * delta)
			if _lever <= 1.0 + 1e-4:
				_lever = 0.999
		else:
			_lever = maxf(0.0, _lever - LEVER_RATE * down_s * delta)
		return
	if up < 0.5 and _lever >= 1.0 - 1e-4 and _lever <= 1.0 + 1e-4:
		_detent_armed = true
	if up_s <= 0.0:
		_detent_timer = 0.0
		return
	if _lever < 1.0:
		_lever = minf(1.0, _lever + LEVER_RATE * up_s * delta)
		_detent_timer = 0.0
		_detent_armed = false
	elif _lever <= 1.0 + 1e-4:
		if up > 0.9 and _detent_armed:
			_detent_timer += delta
			if _detent_timer >= DETENT_HOLD:
				_lever = 1.0 + 1e-3
				_detent_armed = false
		else:
			_detent_timer = 0.0
	else:
		_lever = minf(2.0, _lever + AB_RATE * up_s * delta)


func set_lever(value: float) -> void:
	_lever = clampf(value, 0.0, 2.0)


func lever() -> float:
	return _lever


func detent_progress() -> float:
	return clampf(_detent_timer / DETENT_HOLD, 0.0, 1.0)


func _raw(action: String) -> float:
	return Input.get_action_raw_strength(action)
