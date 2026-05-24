---
id: 0004-create-a-make-target-test-extra
type: feature
status: implementing
title: create a make target 'test-extra'
---

# create a make target 'test-extra'

This make target 'test-extra' runs all 'trunk/load-and-test-*.lisp' scripts, counts all passes/failures to present them in the end.
It should be so flexible that when adding additional 'load-and-test-' scripts they are automatically picked up and also run on 'make text-extra'.
Please ask if anything is unclear.
