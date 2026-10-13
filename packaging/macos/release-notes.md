Install or update OpenLogi on macOS 13 or newer:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh
```

The installer selects Apple Silicon or Intel binaries, verifies their SHA-256 checksum,
and installs them in `~/.local/bin`. Rerunning it updates to the latest release and
restarts an active installation. Your TOML configuration is retained.

Grant `~/.local/bin/openlogid` Accessibility and Input Monitoring access in
System Settings → Privacy & Security, then run:

```sh
~/.local/bin/openlogictl autostart enable
~/.local/bin/openlogictl setup
~/.local/bin/openlogictl status
```

These binaries are signed locally with an ad-hoc signature. They are not notarized
with an Apple Developer ID. macOS may require permissions to be granted again after
an update.
