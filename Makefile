NODE ?= node

.PHONY: build nodejs python setup test api clean help

help:
	@$(NODE) scripts/index.js help

build:
	@$(NODE) scripts/index.js build

nodejs:
	@$(NODE) scripts/index.js nodejs

python:
	@$(NODE) scripts/index.js python

setup:
	@$(NODE) scripts/index.js setup

test:
	@$(NODE) scripts/index.js test

api:
	@$(NODE) scripts/index.js api

clean:
	@$(NODE) scripts/index.js clean
