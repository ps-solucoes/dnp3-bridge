# Changelog

All notable changes to this project are documented in this file. The format
is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-09-26

### Added

- `dnp3-bridge`: a DNP3 outstation (opendnp3 3.1.2, TCP server) fed over
  gRPC. `UpdatePoints` publishes binary and analog values with their quality;
  SCADA commands (CROB, analog outputs) are forwarded to the Python side on
  the `StreamCommands` stream and answered with `RespondToCommand`, with a
  configurable timeout. `GetStatus` reports the outstation state and the
  time of the last update.
- The CEMIG requirements REQ-01 to REQ-13 (`docs/requisitos-cemig.md`):
  unsolicited responses per class, an event buffer of at least 100 events,
  Direct Operate or Select Before Operate, per-point class, deadband and
  variations in `point_database`, 16-bit analog variations and clock sync
  through object 50.
- Configuration from a JSON file overlaid by `DNP3_BRIDGE_*` environment
  variables; logging to stderr and an optional rotating file.
- Debian packages for amd64 and armhf, built in a Debian trixie container.
  `dnp3-bridge` links opendnp3 statically, installs a systemd service that is
  enabled and started on install, and ships `/etc/dnp3-bridge/config.json` as
  a conffile. `dnp3-bridge-tools` ships `dnp3-master-sim`, an interactive
  DNP3 master for testing.

### Fixed

- A `point_database` entry without `deadband` no longer resets the deadband
  set by an earlier entry for the same point.
- Point quality sent over gRPC reaches the DNP3 flags instead of every point
  being reported online. Quality changes alone generate no events or
  unsolicited responses, and the deadband is measured from the last evented
  value.
- A non-finite analog value no longer stops the point's events for good.
- Unknown `class` names, and unknown variation names in the analog sections,
  in `point_database` fail startup instead of silently falling back to
  defaults. The effective point database is logged at startup.

### Known issues

- opendnp3 3.1.2 leaves the master's startup integrity READ unanswered when
  it crosses the outstation's restart null unsolicited response; the master
  recovers after one response timeout.
