# CoF control transport boundary

Actual source: native/guest/cof/control_protocol.*, server/control_dispatch.*, native/csol/client/weapons/guest_cof_control.*, plus the particle manager API and plugin lifecycle wiring.

The namespaced four-byte CoFControl v1 packet is version, operation, body, zero-reserved. Original WpnDeploy sends no original payload and uses MSG_ONE_UNRELIABLE; WeaponBody sends one byte and uses MSG_ONE. The explicit host transport adds framing while preserving these operation-specific reliability choices. Neither registration nor Sent is a client acknowledgment.

The actual receiving adapter is global within the current connection, without equipped-weapon/resource gates. WeaponBody writes current viewmodel.curstate.body only; missing viewmodel does not retain or replay an update. Deploy calls the existing Aurora KillLocal(true), then the proven forward-displacement reset. It does not clear the container or retire audio, particle resources, aim, punch, bob phase or lag history.

Original forward-displacement scalar1055959C is now connected to its verified view consumer; deploy resets only this displacement after the particle operation. Missing backends and callback retirement remain explicit failures; no unused shadow state or forced-success acknowledgment is introduced.

Run the new narrow actual SDK adapter:

    python tests/cryoffear-control-bridge/run.py --environment build/resource-lifecycle-engine-20261108-inputs/environment.json --commands build/resource-bridge-20261108/csol/compile_commands.json --script <original verified gunsmoke.aur> --output build/cof-control-bridge-<fresh-id>

The test runner reuses the existing particle bridge compilation and input-seal implementation, with a different source list. It compiles the actual control/codec/particle/Aurora/forward-displacement TUs with matching SDK flags. The controlled host is not gameplay, original display or performance acceptance. Server sender has separate actual SDK TU compilation; owner event wiring remains the coordinator's work.

ShotState operation3 adds original ViewKnock unsigned-byte pitch subtraction through actual GetViewAngles/SetViewAngles. OrdinaryHost camera capability is an explicit host adaptation, not an invented name for original1018D27C. The original wheelchair pointer10542BB8 contribution has no host implementation. Unsupported camera and missing Get/Set callbacks refuse consumption.
