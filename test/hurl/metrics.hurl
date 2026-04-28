# /metrics endpoint — Prometheus exposer (text exposition format 0.0.4).
# SipiMetrics tracks cache counters, gauges, and request latencies.
GET {{host}}/metrics
HTTP 200
[Asserts]
header "Content-Type" startsWith "text/plain"
header "Content-Type" contains "version=0.0.4"
# At least one HELP / TYPE line must be present (proves a metric is registered).
body matches "(?m)^# HELP "
body matches "(?m)^# TYPE "
