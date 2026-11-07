# experiments/glon-links-multi/Makefile -- two independent link extractors,
# interleaved in one Glon machine. Uses ../glon-links-native/build/glon-web.
#   make test     build glon-web if needed, then run test.sh
#   make docs     regenerate doc-a.html and doc-b.html
N := ../glon-links-native
test:
	$(MAKE) -C $(N) all
	sh test.sh
docs:
	python3 gen_docs.py
.PHONY: test docs
