#!/bin/bash
# NexusAI Comprehensive E2E Test Suite (bash version)
# Preferred: use e2e_full_test.py for more reliable JSON handling
set -e
cd /mnt/c/Users/31677/Desktop/NexusAI/agent-communication-and-tool-selection-framework
python3 e2e_full_test.py "$@"
