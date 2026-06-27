#!/bin/sh
#
# Format Serilog-style JSON log lines for human reading.
# Input format expected: one JSON object per line with fields
#   @timestamp, level, message, optionally WorkflowId.
# Non-JSON lines (kubectl errors, plain logs) pass through unchanged.
#
# Usage in mc-k8s "Pipe through" field:  klog
# Usage from shell:  kubectl logs -f pod | klog
#
# Installation:
#   cp $(mc --datadir)/panel-plugins/k8s/klog.sh ~/.local/bin/klog
#   chmod +x ~/.local/bin/klog
# or use a symlink.

exec jq -rR --unbuffered '
  . as $line
  | try (fromjson | (
      (."@timestamp" // "") as $t
      | (if ($t | length) >= 23 then $t[11:23] else $t end) as $ts
      | (.level // "Info") as $lvl
      | (if (.WorkflowId // "") != ""
           then "wf=" + (.WorkflowId | tostring)[-6:] + " "
           else "" end) as $wf
      | (.message // "") as $msg
      | "\($ts) [\($lvl)] \($wf)\($msg)"
    )) catch $line
' 2>&1
