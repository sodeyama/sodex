# Redirect to the project root makefile.inc (single source of truth)
include $(dir $(lastword $(MAKEFILE_LIST)))/../makefile.inc
