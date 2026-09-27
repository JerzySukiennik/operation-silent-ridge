# One tick of pilot input, already shaped (deadzones, curves, throttle lever with MIL/AB detent); also used by autotest autopilots via Controls.override.
class_name ControlInput
extends RefCounted

var pitch := 0.0          # -1 push (nose down) .. +1 pull (nose up)
var roll := 0.0           # -1 left .. +1 right
var yaw := 0.0            # -1 left .. +1 right (rudder pedals)
var throttle := 0.8       # dry lever, 0 idle .. 1 MIL
var afterburner := 0.0    # 0 off, (0..1] afterburner min..max
var brake := false        # FCS speedbrake (rudders toe-in + flaps)
var gear_toggle := false  # edge: true for one tick
var look := Vector2.ZERO  # camera orbit, -1..1 (x right, y up)
var look_back := false
var help_toggle := false  # edge: true for one tick


func copy_from(o: ControlInput) -> void:
	pitch = o.pitch
	roll = o.roll
	yaw = o.yaw
	throttle = o.throttle
	afterburner = o.afterburner
	brake = o.brake
	gear_toggle = o.gear_toggle
	look = o.look
	look_back = o.look_back
	help_toggle = o.help_toggle


func lever() -> float:
	# single-axis view for the HUD: 0..1 dry, 1..2 afterburner
	return throttle + afterburner if afterburner > 0.0 else throttle
