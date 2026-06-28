#!/usr/bin/env bash
# NativeLink RBE node bootstrap (GCE startup-script). Output is captured to the
# serial console / journald — inspect with:
#   gcloud compute instances get-serial-port-output nativelink-rbe --zone us-central1-a
#
# The VM runs two services behind one mTLS CA:
#   * NativeLink   = CAS + Action Cache + Scheduler + one local Worker (remote exec).
#   * bazel-remote = the http_archive download cache (Remote Asset API, the
#     `--experimental_remote_downloader` backend) — replaces the old Cloud Run proxy.
# Bootstrap steps:
#   1. format + mount the two attached data disks: the pd-ssd at /mnt/ssd (CAS/AC
#      hot tier + worker fast tier + action work_directory; same filesystem,
#      required for hardlink sandboxing) and the larger pd-standard disk at
#      /mnt/slow (the durable CAS + AC slow tier + the bazel-remote cache dir)
#   2. build nativelink from source at the pinned tag (no release binary; the OCI
#      image is distroless / has no shell for actions); install the bazel-remote
#      release binary (pinned version + sha256)
#   3. write the config (from the `nativelink-config` instance-metadata attr)
#   4. install systemd units; nativelink's ExecStartPre BLOCKS until the mTLS
#      material exists in Secret Manager, so the node self-heals once the
#      cert-bootstrap runbook (README) has run; bazel-remote reuses the same certs.
#   5. install the Cloud Ops Agent (guest metrics: memory, per-filesystem disk %;
#      + the systemd journal incl. nativelink/bazel-remote logs) so the VM is
#      observable in Cloud Monitoring/Logging without SSH.
#
# Idempotent: re-running (reboot / metadata re-apply) skips the disk formats, and
# rebuilds/reinstalls only when a pinned version changes.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
export HOME=/root CARGO_HOME=/root/.cargo RUSTUP_HOME=/root/.rustup

md() { curl -sf -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1"; }

echo "=== nativelink bootstrap start $(date -u +%FT%TZ) ==="

# --- 1. data disks: wait for each device, format once, mount by label ---------
# Two attached disks, each its own ext4 filesystem mounted by LABEL (not the
# by-id symlink — survives disk re-attach / enumeration changes; a stale by-id
# entry with `nofail` would silently put the data on the boot disk):
#   /mnt/ssd  (pd-ssd)      — CAS/AC hot tier + worker fast tier + work_directory
#   /mnt/slow (pd-standard) — the durable CAS + AC slow tier
format_mount() { # $1=by-id device  $2=mountpoint  $3=fs label (<=16 chars)
  local dev="$1" mnt="$2" label="$3"
  # The by-id symlink only appears after udev enumerates the attached disk.
  for _ in $(seq 1 30); do [ -e "$dev" ] && break; sleep 1; done
  udevadm settle --timeout=30 || true
  if ! blkid "$dev" >/dev/null 2>&1; then
    echo "formatting $dev as $label"
    mkfs.ext4 -F -m 0 -L "$label" -E lazy_itable_init=0,lazy_journal_init=0 "$dev"
  fi
  mkdir -p "$mnt"
  grep -q " $mnt " /etc/fstab || echo "LABEL=$label $mnt ext4 discard,defaults,nofail 0 2" >>/etc/fstab
  mountpoint -q "$mnt" || mount "$mnt"
}
format_mount /dev/disk/by-id/google-nativelink-ssd  /mnt/ssd  nativelink-cas
format_mount /dev/disk/by-id/google-nativelink-slow /mnt/slow nativelink-slow

# /mnt/ssd: cas/* = CAS_MAIN_STORE hot tier; ac/* = AC_MAIN_STORE hot tier;
# worker/* = the worker's own fast tier (separate LRU; slow tier ref_stores back
# to CAS_MAIN_STORE); work = action sandboxes (must share this filesystem with
# worker/content for hardlinking).
mkdir -p /mnt/ssd/cas/content /mnt/ssd/cas/temp /mnt/ssd/ac/content /mnt/ssd/ac/temp \
  /mnt/ssd/worker/content /mnt/ssd/worker/temp /mnt/ssd/work
# /mnt/slow: the durable CAS + AC slow tier (content + temp on one filesystem,
# required for the store's atomic temp->content rename) + the bazel-remote
# download cache (its own LRU dir).
mkdir -p /mnt/slow/cas/content /mnt/slow/cas/temp /mnt/slow/ac/content /mnt/slow/ac/temp \
  /mnt/slow/bazel-remote

# --- 2. dedicated user + build nativelink from source ------------------------
id nativelink >/dev/null 2>&1 || useradd --system --home /var/lib/nativelink \
  --create-home --shell /usr/sbin/nologin nativelink

NL_VERSION="$(md instance/attributes/nativelink-version)"
SENTINEL=/usr/local/lib/nativelink.version
# Rebuild when the binary is missing OR the pinned tag changed (the sentinel
# records what's installed) — so a version bump self-heals on the next boot.
if [ "$(cat "$SENTINEL" 2>/dev/null || true)" != "$NL_VERSION" ]; then
  echo "building nativelink $NL_VERSION from source"
  apt-get update
  apt-get install -y --no-install-recommends \
    build-essential pkg-config libssl-dev protobuf-compiler cmake clang git curl jq ca-certificates
  # Trust note: the rustup installer is fetched over TLS and run as root, and
  # the source is cloned at a tag (not a content hash). Acceptable for the
  # demonstrator on a trusted VM building our own pinned tag; production should
  # pre-build + sign the binary and verify it here instead of building on boot.
  curl -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable --profile minimal
  export PATH="$CARGO_HOME/bin:$PATH"
  rm -rf /var/tmp/nativelink-src
  git clone --depth 1 --branch "$NL_VERSION" https://github.com/TraceMachina/nativelink /var/tmp/nativelink-src
  ( cd /var/tmp/nativelink-src && cargo build --release --bin nativelink )
  install -m 0755 /var/tmp/nativelink-src/target/release/nativelink /usr/local/bin/nativelink
  mkdir -p "$(dirname "$SENTINEL")"
  echo "$NL_VERSION" >"$SENTINEL"
  rm -rf /var/tmp/nativelink-src
fi

# --- 2b. install bazel-remote (http_archive download cache) ------------------
# Prebuilt release binary, pinned version + sha256-verified. Same trust level as
# the nativelink build above (fetched at boot; production should pre-stage + sign).
# Reinstalls only when the pinned version changes (the sentinel records it).
BR_VERSION="$(md instance/attributes/bazel-remote-version)"
BR_SHA256="$(md instance/attributes/bazel-remote-sha256)"
BR_SENTINEL=/usr/local/lib/bazel-remote.version
if [ "$(cat "$BR_SENTINEL" 2>/dev/null || true)" != "$BR_VERSION" ]; then
  echo "installing bazel-remote $BR_VERSION"
  br_tmp=/var/tmp/bazel-remote
  curl -fsSL -o "$br_tmp" \
    "https://github.com/buchgr/bazel-remote/releases/download/${BR_VERSION}/bazel-remote-${BR_VERSION#v}-linux-amd64"
  echo "${BR_SHA256}  ${br_tmp}" | sha256sum -c -
  install -m 0755 "$br_tmp" /usr/local/bin/bazel-remote
  mkdir -p "$(dirname "$BR_SENTINEL")"; echo "$BR_VERSION" >"$BR_SENTINEL"
  rm -f "$br_tmp"
fi

# --- 3. config + dirs/perms --------------------------------------------------
mkdir -p /etc/nativelink/tls
md instance/attributes/nativelink-config >/etc/nativelink/nativelink.json5
chown -R nativelink:nativelink /mnt/ssd /mnt/slow /etc/nativelink
chmod 0750 /etc/nativelink/tls

# --- 4a. mTLS fetch helper (systemd ExecStartPre) ----------------------------
# Self-contained (quoted heredoc → no provisioning-time interpolation): reads the
# secret names + project from the metadata server at runtime. Fetches to temp
# files and renames only on full success (never leaves a mismatched cert/key
# pair), and BLOCKS until all three secret versions exist — so the service comes
# up automatically once the cert-bootstrap runbook uploads them. It runs as the
# nativelink user into the nativelink-owned tls dir, so no chown is needed.
cat >/usr/local/bin/nativelink-fetch-certs.sh <<'NLFETCH'
#!/usr/bin/env bash
set -euo pipefail
md() { curl -sf -H 'Metadata-Flavor: Google' "http://metadata.google.internal/computeMetadata/v1/$1"; }
fetch_all() {
  local project token crt_id key_id ca_id
  project="$(md project/project-id)" || return 1
  token="$(md instance/service-accounts/default/token | jq -r .access_token)" || return 1
  crt_id="$(md instance/attributes/server-crt-secret)"
  key_id="$(md instance/attributes/server-key-secret)"
  ca_id="$(md instance/attributes/clients-ca-secret)"
  get() { # $1=secret-id  $2=dest
    curl -sf -H "Authorization: Bearer $token" \
      "https://secretmanager.googleapis.com/v1/projects/$project/secrets/$1/versions/latest:access" \
      | jq -r '.payload.data' | base64 -d >"$2"
  }
  get "$crt_id" /etc/nativelink/tls/server.crt.tmp || return 1
  get "$key_id" /etc/nativelink/tls/server.key.tmp || return 1
  get "$ca_id" /etc/nativelink/tls/clients-ca.crt.tmp || return 1
  chmod 0400 /etc/nativelink/tls/server.key.tmp
  chmod 0444 /etc/nativelink/tls/server.crt.tmp /etc/nativelink/tls/clients-ca.crt.tmp
  mv /etc/nativelink/tls/server.crt.tmp /etc/nativelink/tls/server.crt
  mv /etc/nativelink/tls/server.key.tmp /etc/nativelink/tls/server.key
  mv /etc/nativelink/tls/clients-ca.crt.tmp /etc/nativelink/tls/clients-ca.crt
}
until fetch_all; do
  echo "waiting for mTLS secret versions in Secret Manager (run the cert bootstrap)..."
  sleep 30
done
NLFETCH
chmod 0755 /usr/local/bin/nativelink-fetch-certs.sh

# --- 4b. systemd unit --------------------------------------------------------
cat >/etc/systemd/system/nativelink.service <<'NLUNIT'
[Unit]
Description=NativeLink RBE (CAS + AC + scheduler + worker)
After=network-online.target
Wants=network-online.target

[Service]
User=nativelink
Group=nativelink
# ExecStartPre blocks until the mTLS secrets exist; disable the start timeout so
# that wait is never killed. Restart=always covers the main process crashing.
TimeoutStartSec=infinity
ExecStartPre=/usr/local/bin/nativelink-fetch-certs.sh
ExecStart=/usr/local/bin/nativelink /etc/nativelink/nativelink.json5
Restart=always
RestartSec=30
LimitNOFILE=65536
# Hardening. NOTE: IMDS (169.254.169.254) is deliberately NOT blocked — the
# worker process itself needs it for Secret Manager (ADC, the mTLS certs). The
# cost is that a submitted build action can also reach IMDS and the VM SA token;
# the demonstrator bounds this with mTLS gating (only trusted CI holds a client
# cert) + minimal SA IAM (secretAccessor on three secrets). Production must
# isolate per-action network (netns / gVisor) so actions cannot reach IMDS — see
# README.
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=full
ReadWritePaths=/etc/nativelink /mnt/ssd /mnt/slow
ProtectControlGroups=true
RestrictSUIDSGID=true
CapabilityBoundingSet=
AmbientCapabilities=

[Install]
WantedBy=multi-user.target
NLUNIT

systemctl daemon-reload
systemctl enable nativelink.service
# restart, NOT `is-active || start`: on every boot the enabled service
# auto-starts early from the unit file as it existed at boot time, which is
# BEFORE this script rewrites it — so a plain `start` is a no-op and unit changes
# from this run never reach the running process. `restart` forces the
# freshly-written unit to take effect. --no-block so the blocking cert-fetch
# ExecStartPre runs in the background instead of hanging this script.
systemctl restart --no-block nativelink.service

# --- 4c. bazel-remote systemd unit (download cache) --------------------------
# Reuses NativeLink's mTLS material: the server cert's IP SAN is the VM's static
# IP, so it's valid on :50052 too; --tls_ca_file enforces mTLS with the same
# client CA (one client cert authenticates to both :50051 and :50052). Its own
# ExecStartPre waits for the certs the nativelink unit fetches, so it is
# independent of nativelink's health. HTTP binds to localhost — gRPC mTLS on
# :50052 is the only externally-reachable surface (the only port opened besides
# 50051 in the firewall).
cat >/etc/systemd/system/bazel-remote.service <<'BRUNIT'
[Unit]
Description=bazel-remote (http_archive download cache, Remote Asset API)
After=network-online.target
Wants=network-online.target

[Service]
User=nativelink
Group=nativelink
TimeoutStartSec=infinity
ExecStartPre=/bin/bash -c 'until [ -s /etc/nativelink/tls/server.crt ] && [ -s /etc/nativelink/tls/clients-ca.crt ]; do echo "waiting for mTLS certs..."; sleep 5; done'
ExecStart=/usr/local/bin/bazel-remote \
  --dir=/mnt/slow/bazel-remote \
  --max_size=50 \
  --grpc_address=0.0.0.0:50052 \
  --http_address=127.0.0.1:8080 \
  --tls_cert_file=/etc/nativelink/tls/server.crt \
  --tls_key_file=/etc/nativelink/tls/server.key \
  --tls_ca_file=/etc/nativelink/tls/clients-ca.crt \
  --experimental_remote_asset_api
Restart=always
RestartSec=30
LimitNOFILE=65536
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=full
# Whitelist the mount point (consistent with the nativelink unit; the narrower
# /mnt/slow/bazel-remote subdir would work too).
ReadWritePaths=/mnt/slow
ProtectControlGroups=true
# RestrictSUIDSGID is intentionally NOT set here (it IS set on the nativelink unit
# below). bazel-remote v2.6.1 creates its CAS temp files with the setgid bit
# (utils/tempfile/tempfile.go: `wipMode = FinalMode | os.ModeSetgid`, a leftover
# incomplete-file marker). RestrictSUIDSGID installs a seccomp filter that returns
# EPERM on any openat(O_CREAT) whose mode carries setgid — so it breaks EVERY cache
# write ("failed to Put … operation not permitted") and the download cache stores
# nothing. Upstream deleted the setgid code after v2.6.1 (commit a95bc52); restore
# this line once we pin a bazel-remote release that includes that fix. nativelink
# is unaffected — it writes files with plain 0664.
CapabilityBoundingSet=
AmbientCapabilities=

[Install]
WantedBy=multi-user.target
BRUNIT

systemctl daemon-reload
systemctl enable bazel-remote.service
# restart (see the nativelink unit above): forces the freshly-written unit to
# take effect even when the enabled service already auto-started early in boot
# from the previous on-disk unit. --no-block so the cert-wait ExecStartPre
# doesn't hang this script.
systemctl restart --no-block bazel-remote.service

# --- 5. Cloud Ops Agent (guest metrics + systemd journal → Cloud Mon/Logging) -
# Makes the VM observable without SSH: host metrics (memory, per-filesystem disk %)
# plus the systemd journal — which captures the nativelink + bazel-remote service
# logs (NRestarts, evictions, etc.). The official installer is idempotent; install
# only if the agent isn't already running. The journald receiver replaces the
# default syslog pipeline (journald is a superset); host metrics keep their default.
if ! systemctl is-active --quiet google-cloud-ops-agent 2>/dev/null; then
  # The install script is `add-google-cloud-ops-agent-repo.sh` (adds the package
  # repo; `--also-install` then installs the agent). The shorter
  # `add-google-cloud-ops-agent.sh` 404s. `-fsSL` fails loudly on an HTTP error
  # instead of saving an HTML error page and running it as a shell script.
  if curl -fsSL https://dl.google.com/cloudagents/add-google-cloud-ops-agent-repo.sh -o /tmp/add-ops-agent-repo.sh; then
    bash /tmp/add-ops-agent-repo.sh --also-install || echo "WARN: ops-agent install step failed"
    rm -f /tmp/add-ops-agent-repo.sh
  else
    echo "WARN: failed to download ops-agent repo install script"
  fi
fi
mkdir -p /etc/google-cloud-ops-agent
cat >/etc/google-cloud-ops-agent/config.yaml <<'OPSCFG'
logging:
  receivers:
    journald:
      type: systemd_journald
  service:
    pipelines:
      journald:
        receivers: [journald]
OPSCFG
systemctl restart google-cloud-ops-agent || true
echo "=== nativelink bootstrap done $(date -u +%FT%TZ) ==="
