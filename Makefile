PYTHON ?= python3

.PHONY: check compile
check:
	$(PYTHON) -m unittest discover -s tests -v
	$(PYTHON) -m esphome config firmware/opengarage-generic.yaml

compile:
	$(PYTHON) -m esphome compile firmware/opengarage-generic.yaml

