# `MOP` — CLOS Metaobject Protocol

A subset of the **CLOS Metaobject Protocol** (AMOP), exported from the `MOP`
package. The CL-Amiga **closer-mop** shim re-exports these under `CLOSER-MOP`
names, which is how MOP-using Quicklisp libraries (serapeum, trivia, …) reach
them.

- **Package:** `MOP` (uses/used-by `CL` during bootstrap)
- **Inherited by:** `COMMON-LISP-USER`.
- **These follow the AMOP semantics.** This page is a categorized index; the
  authoritative reference for each operator is *The Art of the Metaobject
  Protocol* and the closer-mop documentation. CL-Amiga implements a working
  subset — see `lib/clos.lisp` and the limitation note below for what is and
  isn't covered.

## Class metaobjects

`classp`, `class-direct-superclasses`, `class-direct-subclasses`,
`class-direct-slots`, `class-direct-methods`, `class-direct-default-initargs`,
`class-default-initargs`, `class-precedence-list`, `class-slots`,
`class-effective-slots`, `class-slot-index-table`, `class-finalized-p`,
`class-prototype`, `finalize-inheritance`, `ensure-class`,
`ensure-class-using-class`, `validate-superclass`,
`forward-referenced-class`, `funcallable-standard-class`,
`funcallable-standard-object`, `metaobject`.

## Slot-definition metaobjects

`slot-definition`, `direct-slot-definition`, `effective-slot-definition`,
`standard-slot-definition`, `standard-direct-slot-definition`,
`standard-effective-slot-definition`, `direct-slot-definition-class`,
`effective-slot-definition-class`, `compute-effective-slot-definition`,
`compute-slots`, `slot-definition-name`, `slot-definition-allocation`,
`slot-definition-initargs`, `slot-definition-initform`,
`slot-definition-initfunction`, `slot-definition-type`,
`slot-definition-location`, `slot-definition-readers`,
`slot-definition-writers`, `slot-definition-documentation`.

## Slot access protocol

`slot-value-using-class`, `slot-boundp-using-class`,
`slot-makunbound-using-class`, `standard-instance-access`,
`funcallable-standard-instance-access`, `set-funcallable-instance-function`.

## Generic-function & method metaobjects

`generic-function-name`, `generic-function-lambda-list`,
`generic-function-methods`, `generic-function-method-class`,
`generic-function-method-combination`, `generic-function-declarations`,
`generic-function-argument-precedence-order`,
`ensure-generic-function-using-class`, `compute-discriminating-function`,
`compute-applicable-methods-using-classes`, `compute-effective-method`,
`compute-default-initargs`, `compute-class-precedence-list`,
`make-method-lambda`, `ensure-method`, `method-function`,
`method-generic-function`, `method-lambda-list`, `method-specializers`,
`extract-lambda-list`, `extract-specializer-names`,
`standard-method-combination`, `find-method-combination`.

## Specializers

`specializer`, `specializer-direct-methods`,
`specializer-direct-generic-functions`, `eql-specializer`,
`eql-specializer-p`, `eql-specializer-object`, `intern-eql-specializer`.

## Accessor / reader / writer methods

`standard-accessor-method`, `standard-reader-method`,
`standard-writer-method`, `accessor-method-slot-definition`,
`reader-method-class`, `writer-method-class`.

## Dependent maintenance & class structure mutation

`add-dependent`, `remove-dependent`, `map-dependents`, `update-dependent`,
`add-direct-method`, `remove-direct-method`, `add-direct-subclass`,
`remove-direct-subclass`.

> **Limitation:** the MOP implementation is a working subset, not the complete
> AMOP — see [Known Limitations](../README.md#known-limitations-and-future-work)
> ("full CLOS MOP").

## Source of truth

`tests/test_clos.c` and the CLOS/MOP blocks in `tests/amiga/run-tests.lisp`;
implementation in `lib/clos.lisp`. The closer-mop re-export mapping is in
`contrib/shims/closer-mop/`.
