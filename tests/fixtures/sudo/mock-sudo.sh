#!/bin/sh
# mock-sudo.sh — prints "SUDO: <args>" and exits 0.
# Used by E15-S3 CTests to verify sudo prepend without real privilege escalation.
printf "SUDO:"
for arg in "$@"; do
  printf " %s" "$arg"
done
printf "\n"
exit 0
