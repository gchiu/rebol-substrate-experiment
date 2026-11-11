# experiments/glon-authoring-b7/Makefile
#
# B7: matched A/E completion-contract experiment. B7 reuses the B6 validator
# and B5 verifier unchanged; it does not rerun B or C.

ROOT := ../..
B6   := ../glon-authoring-b6

test:
	cd $(ROOT) && ./check-frozen-s1.sh
	python3 b7_selftest.py
	python3 -m py_compile b7_verify.py b7_selftest.py analyze_b7.py generation/run_opencode_suite.py generation/opencode_extract.py
	$(MAKE) -C $(B6) test

sanitize:
	$(MAKE) -C $(B6) sanitize
	python3 b7_selftest.py

analyze:
	python3 analyze_b7.py trials/AE-seed-20261114 trials/AE-seed-20261115 \
	  --json trials/analysis.json --markdown trials/analysis.md

.PHONY: test sanitize analyze
