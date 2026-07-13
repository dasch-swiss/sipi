---
status: accepted
---

# Native-crash minidumps upload process memory to Sentry — an accepted risk, not yet scrubbed

The Rust shell's crash-reporting cutover (DEV-6659) replaces `sentry-native` with
the Rust `sentry` crate (panics + handled events) plus an out-of-process
`sentry-rust-minidump` reporter for native codec faults (SIGSEGV/SIGBUS/etc.). On
a crash, the reporter reads the crashed process's memory to build a minidump —
register state, stack contents, and referenced heap memory across all
threads — and uploads it to Sentry as an event attachment.

This is a new exposure, not a parity restoration. On `main` (before this cutover),
`sipi server` runs the Rust shell exclusively and never reached the C++ CLI's
`sipi_cli_main`/`init_sentry` path — so the server process never initialized
sentry-native and never captured or uploaded anything on crash. The minidump
reporter added here is the server's first native-crash telemetry of any kind.

**What a minidump can contain, given SIPI's actual request handling:** at the
moment of a crash, worker-thread memory may hold a decoded JWT (bearer auth), a
DSP/Knora session cookie, or in-flight decoded image bytes for restricted or
unpublished humanities-research content. None of this is redacted before upload —
there is no `before_send` hook, no breadcrumb/memory scrubbing, and
`send_default_pii` (false by default) does not apply to raw minidump bytes; it
only gates structured event fields.

## Decision

**Accept the risk. Ship minidump reporting as specified, without a scrubbing
mechanism, for the initial cutover.**

Rationale:

- Codec crashes are rare relative to normal request volume — SIPI's crash-risk
  profile is dominated by malformed/adversarial image input hitting a decoder
  bug, not routine traffic. A crash-triggered upload is a low-frequency event,
  not a standing leak.
- The alternative (build a scrubbing/redaction layer, or restrict minidump
  destinations, before shipping any native-crash visibility) trades a
  low-probability, narrow-window exposure for indefinitely shipping with *zero*
  visibility into the exact class of crash (native codec faults) this whole
  cutover exists to catch — including the already-confirmed DEV-6731 Kakadu
  `exit()`-on-corrupt-JP2 class of bug.
- `sentry-native`'s own inproc backend (the retired predecessor, still active on
  the CLI/oracle path) had the same fundamental property — a native crash
  handler reading process memory to symbolicate a stack trace — without ever
  having had a scrubbing story either. This is not a new category of risk
  DaSCH's Sentry usage hasn't already implicitly accepted; it's a new *path*
  (the internet-facing server, not just the CLI) exercising it.

## Consequences

- No code change accompanies this decision — this document *is* the mitigation
  for now. A future scrubbing mechanism (e.g. a `before_send`/IPC-scope filter
  in `sentry-rust-minidump`, or restricting `SIPI_SENTRY_DSN` to a self-hosted
  Sentry instance under DaSCH's own data-processing agreement rather than
  Sentry.io's public SaaS) is a legitimate follow-up, not a blocker.
- Whoever operates the `SIPI_SENTRY_DSN` value for the production deployment
  should confirm which Sentry instance (self-hosted vs. Sentry.io SaaS) receives
  these events, since that materially changes the data-residency exposure this
  decision accepts on. This ADR does not resolve that question — flagged as an
  open operational item at deploy time (step 14 in the crash-reporting cutover
  plan, or an ops-deploy conversation), not resolved here.
- If a crash minidump ever needs FADP/GDPR breach-notification treatment, this
  ADR is the record of what was known and accepted at cutover time.
