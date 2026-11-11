# experiments/glon-authoring-b6/Makefile
#
# B6: matched A (unvalidated text) and D (text + structured validation).
# B is the B5 historical tagged reference; it is not rerun here.

ROOT := ../..
B5   := ../glon-authoring-b5

test:
	cd $(ROOT) && ./check-frozen-s1.sh
	python3 b6_selftest.py
	python3 -m py_compile b6_verify.py b6_selftest.py analyze_b6.py generation/run_opencode_suite.py generation/opencode_extract.py
	$(MAKE) -C $(B5) test

sanitize:
	$(MAKE) -C ../glon-direct-b2 sanitize
	$(MAKE) -C ../glon-direct-b3 sanitize
	$(MAKE) -C ../glon-source-b4 sanitize
	$(MAKE) -C $(B5) sanitize
	python3 b6_selftest.py

analyze:
	python3 analyze_b6.py trials/AD-seed-20261112 trials/AD-seed-20261113 \
	  --json trials/analysis.json --markdown trials/analysis.md

.PHONY: test sanitize analyze
