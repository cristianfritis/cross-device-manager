# daemon-hardening Delta

## ADDED Requirements

### Requirement: Systemd sandboxing baseline
The shipped `devmgrd.service` SHALL declare a reviewed sandboxing set (at minimum: `ProtectHome`, `ProtectSystem=strict` with explicit `ReadWritePaths` for the state dir and modprobe dir, `PrivateTmp`, `RestrictAddressFamilies` limited to what D-Bus and netlink need, and a bounded capability set). Every directive SHALL be justified in a comment or the design doc, and the acceptance suite SHALL prove all privileged flows (enable/disable, module ops, snapshot restore, firmware check) still work under the sandbox.

#### Scenario: Privileged flows work sandboxed
- **WHEN** the acceptance suite runs against the packaged daemon under the hardened unit
- **THEN** all privileged scenarios pass and the journal shows no sandbox denials for supported operations

### Requirement: IPC input validation on every verb
Every `org.devmgr.Manager1` verb SHALL validate its arguments before acting: length caps on all string inputs, id charset checks (hex-only where ids are expected), label printable-charset and length rules, and size caps on JSON payloads. Invalid input SHALL fail with `InvalidArgs` (wire: `org.devmgr.Error.InvalidArgs`, added in ApiVersion 4), change no state, and never crash or allocate unboundedly. `InvalidArgs` is distinct from `NotFound`: malformed request versus a well-formed request naming something absent. Pre-v4 clients do not know the name and collapse it to `Io` per the existing unknown-error rule.

#### Scenario: Oversized input refused
- **WHEN** a client calls a verb with a multi-megabyte string argument
- **THEN** the daemon refuses with InvalidArgs without processing the payload and remains responsive

### Requirement: Privilege minimization audit
A documented audit SHALL enumerate what the daemon reads, writes, and executes; anything not required by a spec'd capability SHALL be removed or blocked by the sandbox. The audit result (what is required and why) SHALL live in the repo docs.

#### Scenario: Audit matches sandbox
- **WHEN** the audit document and the unit file are compared
- **THEN** every allowed path/capability in the unit maps to a documented need, with no unexplained allowances
